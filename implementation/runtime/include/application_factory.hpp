// Copyright (C) 2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_APPLICATION_FACTORY_HPP_
#define VSOMEIP_V3_APPLICATION_FACTORY_HPP_

#include <memory>
#include <string>

namespace boost {
namespace asio {
class io_context;
} // namespace asio
} // namespace boost

namespace vsomeip_v3 {

class application;

namespace internal {

// Indirection that lets runtime_impl (in libvsomeip3-core.so) forward its
// application-related vtable slots to an implementation in the routing
// layer (see runtime_impl_apps.cpp) without core needing application_impl
// itself. Registered into runtime_impl at library-load time via
// runtime_impl::set_application_factory.
class application_factory {
public:
    virtual ~application_factory() = default;

    virtual std::shared_ptr<application> create(const std::string& _name, const std::string& _path) = 0;

    // Backs create_application_with_external_io.
    virtual std::shared_ptr<application> create_with_io(const std::string& _name, const std::string& _path,
                                                        boost::asio::io_context& _io) = 0;

    virtual std::shared_ptr<application> get(const std::string& _name) const = 0;

    virtual void remove(const std::string& _name) = 0;
};

} // namespace internal
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_APPLICATION_FACTORY_HPP_
