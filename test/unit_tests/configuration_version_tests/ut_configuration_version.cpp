// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <vsomeip/constants.hpp>
#include <vsomeip/primitive_types.hpp>

#include "../../../implementation/configuration/include/configuration_impl.hpp"

namespace {

// Minimal configuration that exercises the per-service `major`/`minor` keys
// consumed by `configuration_impl::get_major_version` / `get_minor_version`.
//
//   0x1234/0x0001 : version given as hex major + decimal minor.
//   0x1236/0x0003 : version given as decimal major + hex minor.
//   0x1235/0x0002 : neither key present -> struct defaults are used.
const char* const kConfigJson = R"({
    "unicast" : "127.0.0.1",
    "logging" : { "level" : "warning", "console" : "false", "file" : { "enable" : "false" } },
    "applications" : [ { "name" : "version_test", "id" : "0x1277" } ],
    "routing" : "version_test",
    "service-discovery" : { "enable" : "false" },
    "services" :
    [
        {
            "service" : "0x1234",
            "instance" : "0x0001",
            "major" : "0x03",
            "minor" : "5",
            "unreliable" : "30509"
        },
        {
            "service" : "0x1236",
            "instance" : "0x0003",
            "major" : "2",
            "minor" : "0x0000000A",
            "unreliable" : "30510"
        },
        {
            "service" : "0x1235",
            "instance" : "0x0002",
            "unreliable" : "30511"
        }
    ]
})";

std::string temp_dir() {
    if (const char* tmp = std::getenv("TEST_TMPDIR")) {
        return std::string(tmp) + "/";
    }
    return "/tmp/";
}

class configuration_version_test : public ::testing::Test {
protected:
    void SetUp() override {
        config_path_ = temp_dir() + "ut_configuration_version.json";

        std::ofstream out(config_path_);
        ASSERT_TRUE(out.is_open()) << "Failed to create config file: " << config_path_;
        out << kConfigJson;
        out.close();

        // Ensure an inherited environment variable does not shadow the file we
        // just wrote (configuration_impl::load honours VSOMEIP_CONFIGURATION).
#if defined(__linux__) || defined(__QNX__)
        unsetenv("VSOMEIP_CONFIGURATION");
#endif

        config_ = std::make_shared<vsomeip_v3::cfg::configuration_impl>(config_path_);
        ASSERT_TRUE(config_->load("version_test"));
    }

    void TearDown() override {
        config_.reset();
        std::remove(config_path_.c_str());
    }

    std::string config_path_;
    std::shared_ptr<vsomeip_v3::cfg::configuration_impl> config_;
};

TEST_F(configuration_version_test, parses_hex_major_and_decimal_minor) {
    EXPECT_EQ(config_->get_major_version(0x1234, 0x0001), static_cast<vsomeip_v3::major_version_t>(0x03));
    EXPECT_EQ(config_->get_minor_version(0x1234, 0x0001), static_cast<vsomeip_v3::minor_version_t>(5));
}

TEST_F(configuration_version_test, parses_decimal_major_and_hex_minor) {
    EXPECT_EQ(config_->get_major_version(0x1236, 0x0003), static_cast<vsomeip_v3::major_version_t>(2));
    EXPECT_EQ(config_->get_minor_version(0x1236, 0x0003), static_cast<vsomeip_v3::minor_version_t>(10));
}

TEST_F(configuration_version_test, unspecified_version_uses_defaults) {
    EXPECT_EQ(config_->get_major_version(0x1235, 0x0002), vsomeip_v3::DEFAULT_MAJOR);
    EXPECT_EQ(config_->get_minor_version(0x1235, 0x0002), vsomeip_v3::DEFAULT_MINOR);
}

TEST_F(configuration_version_test, unknown_service_uses_defaults) {
    EXPECT_EQ(config_->get_major_version(0x9999, 0x9999), vsomeip_v3::DEFAULT_MAJOR);
    EXPECT_EQ(config_->get_minor_version(0x9999, 0x9999), vsomeip_v3::DEFAULT_MINOR);
}

} // namespace
