// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Regression tests for runtime_impl's application-factory slot. Verify that:
//   * a set factory is dispatched to on create_application/get_application/remove_application,
//   * a null slot produces the documented failure modes on those calls,
//   * a replacement of the slot while a call is dispatching does NOT free
//     the factory the in-flight call is using (the shared_ptr property),
//   * concurrent set + create_application are race-free (TSan validates this).
//
// All observation is via the public `runtime` interface — the tests hold
// their own shared_ptr to the test-double factory to inspect its counters,
// but never touch runtime_impl::get_application_factory directly.

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>

#include <gtest/gtest.h>

#include "../../../implementation/runtime/include/application_factory.hpp"
#include "../../../implementation/runtime/include/runtime_impl.hpp"

namespace {

// Test-double factory. Every method is a no-op that returns nullptr; the
// test cares about *whether* a method was dispatched to and *whether* the
// object is still alive, both of which are observable via a shared atomic.
class tracked_factory : public vsomeip_v3::internal::application_factory {
public:
    struct counters {
        std::atomic<int> live_count{0};
        std::atomic<int> create_count{0};
        std::atomic<int> get_count{0};
        std::atomic<int> remove_count{0};
    };

    explicit tracked_factory(std::shared_ptr<counters> _c) : counters_(std::move(_c)) {
        counters_->live_count.fetch_add(1);
    }
    ~tracked_factory() override { counters_->live_count.fetch_sub(1); }

    std::shared_ptr<vsomeip_v3::application> create(const std::string&, const std::string&) override {
        counters_->create_count.fetch_add(1);
        return nullptr;
    }
    std::shared_ptr<vsomeip_v3::application> create_with_io(const std::string&, const std::string&,
                                                             boost::asio::io_context&) override {
        counters_->create_count.fetch_add(1);
        return nullptr;
    }
    std::shared_ptr<vsomeip_v3::application> get(const std::string&) const override {
        counters_->get_count.fetch_add(1);
        return nullptr;
    }
    void remove(const std::string&) override { counters_->remove_count.fetch_add(1); }

private:
    std::shared_ptr<counters> counters_;
};

// A factory whose create() blocks until the test lets it proceed. Lets us
// prove the "replace while dispatching" property from outside runtime_impl.
class blocking_factory : public vsomeip_v3::internal::application_factory {
public:
    blocking_factory(std::shared_ptr<std::atomic<int>> _live_count, std::future<void> _unblock,
                     std::promise<void>& _entered)
        : live_count_(std::move(_live_count)), unblock_(std::move(_unblock)), entered_(_entered) {
        live_count_->fetch_add(1);
    }
    ~blocking_factory() override { live_count_->fetch_sub(1); }

    std::shared_ptr<vsomeip_v3::application> create(const std::string&, const std::string&) override {
        entered_.set_value();
        unblock_.wait();
        return nullptr;
    }
    std::shared_ptr<vsomeip_v3::application> create_with_io(const std::string&, const std::string&,
                                                             boost::asio::io_context&) override {
        return nullptr;
    }
    std::shared_ptr<vsomeip_v3::application> get(const std::string&) const override { return nullptr; }
    void remove(const std::string&) override {}

private:
    std::shared_ptr<std::atomic<int>> live_count_;
    std::future<void> unblock_;
    std::promise<void>& entered_;
};

// Give each test a clean, known slot. TearDown drains so no test leaves
// its factory behind for the next.
class runtime_impl_factory_test : public ::testing::Test {
protected:
    void SetUp() override { vsomeip_v3::runtime_impl::set_application_factory(nullptr); }
    void TearDown() override { vsomeip_v3::runtime_impl::set_application_factory(nullptr); }
};

} // namespace

TEST_F(runtime_impl_factory_test, installed_factory_receives_dispatches) {
    auto c = std::make_shared<tracked_factory::counters>();
    vsomeip_v3::runtime_impl::set_application_factory(std::make_shared<tracked_factory>(c));

    auto rt = vsomeip_v3::runtime::get();
    rt->create_application("app1");
    rt->create_application("app2", "/some/path");
    rt->get_application("app3");
    rt->remove_application("app4");

    EXPECT_EQ(c->create_count.load(), 2);
    EXPECT_EQ(c->get_count.load(), 1);
    EXPECT_EQ(c->remove_count.load(), 1);
}

TEST_F(runtime_impl_factory_test, null_slot_produces_documented_failure_modes) {
    auto rt = vsomeip_v3::runtime::get();

    // create_application throws.
    EXPECT_THROW(rt->create_application("nope"), std::logic_error);
    EXPECT_THROW(rt->create_application("nope", "/path"), std::logic_error);

    // get_application returns nullptr.
    EXPECT_EQ(rt->get_application("nope"), nullptr);

    // remove_application is a no-op (must not crash).
    EXPECT_NO_THROW(rt->remove_application("nope"));
}

TEST_F(runtime_impl_factory_test, replacing_slot_during_in_flight_dispatch_is_safe) {
    // Install a factory whose create() blocks. Start a dispatcher thread
    // that enters that create(); wait for it to be provably in-flight.
    auto live_count = std::make_shared<std::atomic<int>>(0);
    std::promise<void> entered;
    auto entered_fut = entered.get_future();
    std::promise<void> unblock;
    auto blocking = std::make_shared<blocking_factory>(live_count, unblock.get_future(), entered);
    vsomeip_v3::runtime_impl::set_application_factory(blocking);

    std::thread dispatcher{[] {
        vsomeip_v3::runtime::get()->create_application("app");
    }};

    entered_fut.wait();

    // At this point the dispatcher is inside blocking_factory::create(),
    // holding a shared_ptr<application_factory> to the SAME object we
    // installed above. Replace the slot with a fresh factory.
    auto other_counters = std::make_shared<tracked_factory::counters>();
    vsomeip_v3::runtime_impl::set_application_factory(std::make_shared<tracked_factory>(other_counters));

    // The blocking_factory is no longer in the slot, but its live_count
    // must still be 1 because the dispatcher is still using it. Also
    // release our own shared_ptr to it — the dispatcher should now be the
    // sole owner.
    blocking.reset();
    EXPECT_EQ(live_count->load(), 1);

    // Let the dispatcher finish. Its shared_ptr goes out of scope on
    // return, blocking_factory finally dies.
    unblock.set_value();
    dispatcher.join();
    EXPECT_EQ(live_count->load(), 0);
}

TEST_F(runtime_impl_factory_test, concurrent_dispatch_and_replace_are_race_free) {
    // N readers dispatch through runtime::create_application while a
    // writer replaces the slot. Correctness signals: no crash / UAF
    // (asserted implicitly by finishing the test), and — after draining
    // the slot — no tracked_factory instance leaks.
    auto counters = std::make_shared<tracked_factory::counters>();
    vsomeip_v3::runtime_impl::set_application_factory(std::make_shared<tracked_factory>(counters));

    constexpr int kReaderCount = 4;
    constexpr int kWriteIterations = 500;
    constexpr int kReaderIterations = 5'000;

    std::atomic<bool> writer_done{false};

    std::vector<std::thread> readers;
    readers.reserve(kReaderCount);
    for (int i = 0; i < kReaderCount; ++i) {
        readers.emplace_back([&] {
            auto rt = vsomeip_v3::runtime::get();
            for (int j = 0; j < kReaderIterations; ++j) {
                try {
                    rt->create_application("hammer");
                } catch (const std::logic_error&) {
                    // Writer may briefly install nullptr; not a bug.
                }
                if (writer_done.load(std::memory_order_relaxed)) return;
            }
        });
    }

    std::thread writer{[&] {
        for (int i = 1; i <= kWriteIterations; ++i) {
            if ((i % 10) == 0) {
                vsomeip_v3::runtime_impl::set_application_factory(nullptr);
            } else {
                vsomeip_v3::runtime_impl::set_application_factory(std::make_shared<tracked_factory>(counters));
            }
            std::this_thread::yield();
        }
        writer_done.store(true, std::memory_order_relaxed);
    }};

    writer.join();
    for (auto& t : readers) t.join();

    vsomeip_v3::runtime_impl::set_application_factory(nullptr);
    EXPECT_EQ(counters->live_count.load(), 0);
}
