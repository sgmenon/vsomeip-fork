// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_RUNTIME_IMPL_HPP_
#define VSOMEIP_V3_RUNTIME_IMPL_HPP_

#include <vsomeip/runtime.hpp>
#include <vsomeip/application_ext.hpp>

#include <map>
#include <memory>

#include <boost/asio/io_context.hpp>

#include "application_factory.hpp"

namespace vsomeip_v3 {

// runtime_impl lives in libvsomeip3-core.so. Its create_application /
// get_application / remove_application vtable slots forward through an
// `application_factory` that the routing layer installs at library load
// (see runtime_impl_apps.cpp) — keeping `application_impl` out of core
// while leaving the vtable resolved.
class runtime_impl : public runtime {
public:
    static std::string get_property(const std::string& _name);
    static void set_property(const std::string& _name, const std::string& _value);

    static std::shared_ptr<runtime> get();

    // Install the application factory (from the routing layer's registrar).
    // Nullable: with no factory installed (SD-only host), create_application
    // throws std::logic_error, get_application returns nullptr, and
    // remove_application is a no-op. Thread-safe.
    static void set_application_factory(std::shared_ptr<internal::application_factory> _factory);

    virtual ~runtime_impl() = default;

    std::shared_ptr<application> create_application(const std::string& _name);
    std::shared_ptr<application> create_application(const std::string& _name, const std::string& _path);

    std::shared_ptr<application> create_application(
            const std::string& _name, const std::string& _path, boost::asio::io_context& _io);

    std::shared_ptr<message> create_message(bool _reliable) const;
    std::shared_ptr<message> create_request(bool _reliable) const;
    std::shared_ptr<message> create_response(const std::shared_ptr<message>& _request) const;
    std::shared_ptr<message> create_notification(bool _reliable) const;

    std::shared_ptr<payload> create_payload() const;
    std::shared_ptr<payload> create_payload(const byte_t* _data, uint32_t _size) const;
    std::shared_ptr<payload> create_payload(const std::vector<byte_t>& _data) const;

    std::shared_ptr<application> get_application(const std::string& _name) const;

    void remove_application(const std::string& _name);

private:
    // Not on the API surface. Only friend is the routing-layer
    // create_application_with_external_io shortcut.
    static std::shared_ptr<internal::application_factory> get_application_factory();

    friend std::shared_ptr<application> create_application_with_external_io(const std::string& _name,
                                                                             boost::asio::io_context& _io);

    static std::map<std::string, std::string> properties_;
};

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_RUNTIME_IMPL_HPP_
