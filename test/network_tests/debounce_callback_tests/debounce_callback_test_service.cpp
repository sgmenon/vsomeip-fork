// Copyright (C) 2023 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <vsomeip/internal/logger.hpp>

#include "debounce_callback_test_service.hpp"

debounce_test_service::debounce_test_service() :
    runner_(std::bind(&debounce_test_service::run, this)),
    app_(vsomeip::runtime::get()->create_application("debounce_timeout_test_service")) { }

bool debounce_test_service::init() {

    bool is_initialized = app_->init();
    if (is_initialized) {
        app_->register_message_handler(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, DEBOUNCE_START_METHOD,
                                       std::bind(&debounce_test_service::on_start, this, std::placeholders::_1));
        app_->register_message_handler(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, DEBOUNCE_STOP_METHOD,
                                       std::bind(&debounce_test_service::on_stop, this, std::placeholders::_1));
        app_->offer_event(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, DEBOUNCE_EVENT, {DEBOUNCE_EVENTGROUP}, vsomeip::event_type_e::ET_FIELD,
                          std::chrono::milliseconds::zero(), false, true, nullptr, vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->offer_event(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, DEBOUNCE_EVENT_2, {DEBOUNCE_EVENTGROUP}, vsomeip::event_type_e::ET_FIELD,
                          std::chrono::milliseconds::zero(), false, true, nullptr, vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->offer_service(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, DEBOUNCE_MAJOR, DEBOUNCE_MINOR);
    }
    return is_initialized;
}

void debounce_test_service::start() {

    VSOMEIP_INFO << "Starting Service...";
    app_->start();
}

void debounce_test_service::stop() {

    VSOMEIP_INFO << "Stopping Service...";
    app_->stop();
}

void debounce_test_service::run() {

    {
        std::unique_lock<std::mutex> its_lock(run_mutex_);
        auto its_result = run_condition_.wait_for(its_lock, std::chrono::seconds(10));
        if (its_result == std::cv_status::timeout)
            return;
    }

    start_test();
}

void debounce_test_service::wait() {

    if (runner_.joinable())
        runner_.join();
}

void debounce_test_service::on_start(const std::shared_ptr<vsomeip::message>& _message) {

    (void)_message;

    VSOMEIP_INFO << __func__ << ": Starting test";
    run_condition_.notify_one();
}

void debounce_test_service::on_stop(const std::shared_ptr<vsomeip::message>& _message) {

    (void)_message;

    VSOMEIP_INFO << __func__ << ": Received a STOP command.";
    stop();
}

namespace {

std::shared_ptr<vsomeip::payload> make_payload(std::initializer_list<vsomeip::byte_t> _data) {
    auto its_payload = vsomeip::runtime::get()->create_payload();
    its_payload->set_data(std::vector<vsomeip::byte_t>(_data));
    return its_payload;
}

void notify_event(const std::shared_ptr<vsomeip::application>& _app, vsomeip::event_t _event,
                  std::initializer_list<vsomeip::byte_t> _data) {
    _app->notify(DEBOUNCE_SERVICE, DEBOUNCE_INSTANCE, _event, make_payload(_data));
}

} // namespace

void debounce_test_service::start_test() {

    // Fresh payload per notify (moved in). Do not mutate a shared payload after notify.

    // To check if debouncer send the last message after timeout
    notify_event(app_, DEBOUNCE_EVENT, {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    VSOMEIP_INFO << "Sent first message!";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent second message!";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // To check normal function of the debouncer
    notify_event(app_, DEBOUNCE_EVENT, {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    VSOMEIP_INFO << "Sent third message!";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent fourth message!";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // To check only one message is forwarded after timeout
    notify_event(app_, DEBOUNCE_EVENT, {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    VSOMEIP_INFO << "Sent fifth message!";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent sixth message!";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent seventh message!";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // To check the interaction when two events use the debounce
    notify_event(app_, DEBOUNCE_EVENT, {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    VSOMEIP_INFO << "Sent eight message!";
    notify_event(app_, DEBOUNCE_EVENT_2, {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    VSOMEIP_INFO << "Sent first event 2 message!";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent ninth message!";
    notify_event(app_, DEBOUNCE_EVENT_2, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent second event 2 message!";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    notify_event(app_, DEBOUNCE_EVENT, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent tenth message!";
    notify_event(app_, DEBOUNCE_EVENT_2, {0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09});
    VSOMEIP_INFO << "Sent third event 2 message!";

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Testing with lots of messages

    VSOMEIP_INFO << "Sending lot of messages!";
    for (int i = 0; i < 30; i++) {
        const auto its_data = std::initializer_list<vsomeip::byte_t>{0x00, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                                     static_cast<vsomeip::byte_t>(i % 16)};
        notify_event(app_, DEBOUNCE_EVENT, its_data);
        notify_event(app_, DEBOUNCE_EVENT_2, its_data);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

TEST(debounce_timeout_test, callback) {
    debounce_test_service its_service;
    ASSERT_TRUE(its_service.init());
    its_service.start();
    its_service.wait();
}

int main(int argc, char** argv) {

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
