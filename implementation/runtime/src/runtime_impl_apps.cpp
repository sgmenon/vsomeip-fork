// Copyright (C) 2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Routing-layer half of `runtime_impl`: application registry + factory
// implementation, installed into core's runtime_impl slot at library load
// via the namespace-scope registrar_ below. Only in libvsomeip3.so — an
// SD-only host never runs this and therefore has no factory installed,
// which is what causes runtime_impl::create_application to throw
// std::logic_error.

#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <utility>

#include <boost/asio/io_context.hpp>

#include "../include/application_impl.hpp"
#include "../include/application_factory.hpp"
#include "../include/runtime_impl.hpp"

namespace vsomeip_v3 {

namespace {

class application_factory_impl : public internal::application_factory {
public:
    std::shared_ptr<application> create(const std::string& _name, const std::string& _path) override {
        std::scoped_lock lock{applications_mutex_};
        const std::string its_name = pick_unique_name_locked(_name);
        auto app = std::make_shared<application_impl>(its_name, _path);
        applications_[its_name] = app;
        return app;
    }

    std::shared_ptr<application> create_with_io(const std::string& _name, const std::string& _path,
                                                boost::asio::io_context& _io) override {
        std::scoped_lock lock{applications_mutex_};
        const std::string its_name = pick_unique_name_locked(_name);
        auto app = std::make_shared<application_impl>(its_name, _path, _io);
        applications_[its_name] = app;
        return app;
    }

    std::shared_ptr<application> get(const std::string& _name) const override {
        std::scoped_lock lock{applications_mutex_};
        auto found_application = applications_.find(_name);
        if (found_application != applications_.end())
            return found_application->second.lock();
        return nullptr;
    }

    void remove(const std::string& _name) override {
        std::scoped_lock lock{applications_mutex_};
        applications_.erase(_name);
    }

private:
    // Appends a monotonic postfix if the name is already taken (preserves the
    // pre-split behaviour). Caller must hold applications_mutex_.
    std::string pick_unique_name_locked(const std::string& _name) {
        static std::uint32_t postfix_id = 0;
        if (applications_.find(_name) == applications_.end())
            return _name;
        return _name + "_" + std::to_string(postfix_id++);
    }

    mutable std::mutex applications_mutex_;
    std::map<std::string, std::weak_ptr<application>> applications_;
};

// Runs at library init (before main), installing the factory in core.
struct factory_registrar {
    factory_registrar() {
        runtime_impl::set_application_factory(std::make_shared<application_factory_impl>());
    }
};

const factory_registrar registrar_{};

} // namespace

// Declared in <vsomeip/application_ext.hpp>. Shortcut around
// runtime::create_application(name, path, io) that goes straight to the
// factory.
std::shared_ptr<application> create_application_with_external_io(const std::string& _name, boost::asio::io_context& _io) {
    auto factory = runtime_impl::get_application_factory();
    if (!factory) {
        throw std::logic_error(
                "vsomeip: create_application_with_external_io called but no application_factory has been registered — "
                "this build is missing the routing layer (libvsomeip3.so).");
    }
    return factory->create_with_io(_name, /*_path=*/"", _io);
}

} // namespace vsomeip_v3
