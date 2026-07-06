// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

#include <vsomeip/constants.hpp>
#include <vsomeip/defines.hpp>

#include "../include/runtime_impl.hpp"
#include "../../message/include/message_impl.hpp"
#include "../../message/include/payload_impl.hpp"

namespace vsomeip_v3 {

std::map<std::string, std::string> runtime_impl::properties_;

std::string runtime_impl::get_property(const std::string& _name) {
    auto found_property = properties_.find(_name);
    if (found_property != properties_.end())
        return found_property->second;
    return "";
}

void runtime_impl::set_property(const std::string& _name, const std::string& _value) {
    properties_[_name] = _value;
}

std::shared_ptr<runtime> runtime_impl::get() {
    static std::shared_ptr<runtime> the_runtime_ = std::make_shared<runtime_impl>();
    return the_runtime_;
}

// Application-factory slot: installed by the routing layer's registrar (see
// runtime_impl_apps.cpp) at library load; read below by create_application /
// get_application / remove_application. `shared_ptr` (not `unique_ptr`) so a
// concurrent set replacing the slot cannot free the factory out from under
// an in-flight get. Accessed via C++17's atomic-on-shared_ptr free
// functions to keep the read path lock-free; swap for
// `std::atomic<std::shared_ptr<T>>` when this codebase moves to C++20.
namespace {

std::shared_ptr<internal::application_factory>& factory_slot() {
    // Function-local static: constructed on first use, ahead of any pre-main
    // registrar that might reach it.
    static std::shared_ptr<internal::application_factory> instance;
    return instance;
}

[[noreturn]] void throw_no_factory(const char* _method) {
    throw std::logic_error(std::string("vsomeip: ") + _method
                           + " called but no application_factory has been"
                             " registered — this build is missing the routing"
                             " layer (libvsomeip3.so). See runtime_impl_apps.cpp.");
}

} // namespace

void runtime_impl::set_application_factory(std::shared_ptr<internal::application_factory> _factory) {
    std::atomic_store_explicit(&factory_slot(), std::move(_factory), std::memory_order_release);
}

std::shared_ptr<internal::application_factory> runtime_impl::get_application_factory() {
    return std::atomic_load_explicit(&factory_slot(), std::memory_order_acquire);
}

std::shared_ptr<application> runtime_impl::create_application(const std::string& _name) {
    return create_application(_name, "");
}

std::shared_ptr<application> runtime_impl::create_application(const std::string& _name, const std::string& _path) {
    auto factory = get_application_factory();
    if (!factory)
        throw_no_factory("runtime::create_application");
    return factory->create(_name, _path);
}

std::shared_ptr<application> runtime_impl::create_application(const std::string& _name, const std::string& _path,
                                                              boost::asio::io_context& _io) {
    auto factory = get_application_factory();
    if (!factory)
        throw_no_factory("runtime::create_application");
    return factory->create_with_io(_name, _path, _io);
}

std::shared_ptr<application> runtime_impl::get_application(const std::string& _name) const {
    // No factory installed = SD-only host, no apps to find. Match the
    // "unknown name" contract by returning nullptr rather than throwing.
    auto factory = get_application_factory();
    if (!factory)
        return nullptr;
    return factory->get(_name);
}

void runtime_impl::remove_application(const std::string& _name) {
    if (auto factory = get_application_factory())
        factory->remove(_name);
}


std::shared_ptr<message> runtime_impl::create_message(bool _reliable) const {
    auto its_message = std::make_shared<message_impl>();
    its_message->set_protocol_version(VSOMEIP_PROTOCOL_VERSION);
    its_message->set_return_code(return_code_e::E_OK);
    its_message->set_reliable(_reliable);
    its_message->set_interface_version(DEFAULT_MAJOR);
    return its_message;
}

std::shared_ptr<message> runtime_impl::create_request(bool _reliable) const {
    auto its_request = std::make_shared<message_impl>();
    its_request->set_protocol_version(VSOMEIP_PROTOCOL_VERSION);
    its_request->set_message_type(message_type_e::MT_REQUEST);
    its_request->set_return_code(return_code_e::E_OK);
    its_request->set_reliable(_reliable);
    its_request->set_interface_version(DEFAULT_MAJOR);
    return its_request;
}

std::shared_ptr<message> runtime_impl::create_response(const std::shared_ptr<message>& _request) const {
    auto its_response = std::make_shared<message_impl>();
    its_response->set_service(_request->get_service());
    its_response->set_instance(_request->get_instance());
    its_response->set_method(_request->get_method());
    its_response->set_client(_request->get_client());
    its_response->set_session(_request->get_session());
    its_response->set_interface_version(_request->get_interface_version());
    its_response->set_message_type(message_type_e::MT_RESPONSE);
    its_response->set_return_code(return_code_e::E_OK);
    its_response->set_reliable(_request->is_reliable());
    return its_response;
}

std::shared_ptr<message> runtime_impl::create_notification(bool _reliable) const {
    auto its_notification = std::make_shared<message_impl>();
    its_notification->set_protocol_version(VSOMEIP_PROTOCOL_VERSION);
    its_notification->set_message_type(message_type_e::MT_NOTIFICATION);
    its_notification->set_return_code(return_code_e::E_OK);
    its_notification->set_reliable(_reliable);
    its_notification->set_interface_version(DEFAULT_MAJOR);
    return its_notification;
}

std::shared_ptr<payload> runtime_impl::create_payload() const {
    return std::make_shared<payload_impl>();
}

std::shared_ptr<payload> runtime_impl::create_payload(const byte_t* _data, uint32_t _size) const {
    return std::make_shared<payload_impl>(_data, _size);
}

std::shared_ptr<payload> runtime_impl::create_payload(const std::vector<byte_t>& _data) const {
    return std::make_shared<payload_impl>(_data);
}

} // namespace vsomeip_v3
