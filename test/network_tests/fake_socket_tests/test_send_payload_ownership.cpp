// Copyright (C) 2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <vsomeip/vsomeip.hpp>
#include <gtest/gtest.h>

namespace {

// application::send decides snapshot at the API boundary. With a completion
// handler the caller's message payload is left alone; without one, a shared
// message gets a snapshot via set_payload.
TEST(application_send_payload_ownership, skips_snapshot_when_completion_provided) {
    auto app = vsomeip::runtime::get()->create_application("send_payload_ownership");
    ASSERT_NE(app, nullptr);
    ASSERT_TRUE(app->init());

    auto message = vsomeip::runtime::get()->create_request(true);
    message->set_service(0x3344);
    message->set_instance(0x1);
    message->set_method(0x1111);
    auto payload = vsomeip::runtime::get()->create_payload();
    payload->set_data(std::vector<vsomeip::byte_t>{0x01, 0x02, 0x03});
    message->set_payload(payload);
    auto kept_message = message;
    auto* original_payload = message->get_payload().get();

    app->send(message, [](bool) {});
    EXPECT_EQ(message->get_payload().get(), original_payload);

    app->send(message);
    EXPECT_NE(message->get_payload().get(), original_payload);
    (void)kept_message;

    app->clear_all_handler();
}

} // namespace
