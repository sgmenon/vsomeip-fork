// Copyright (C) 2024 General Motors
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_APPLICATION_EXT_HPP_
#define VSOMEIP_V3_APPLICATION_EXT_HPP_

#include <memory>
#include <string>

#include <vsomeip/export.hpp>

namespace boost { namespace asio { class io_context; } }

namespace vsomeip_v3 {

class application;

VSOMEIP_IMPORT_EXPORT std::shared_ptr<application> create_application_with_external_io(
    const std::string& _name, boost::asio::io_context& _io);

} // namespace vsomeip_v3

namespace vsomeip = vsomeip_v3;

#endif // VSOMEIP_V3_APPLICATION_EXT_HPP_
