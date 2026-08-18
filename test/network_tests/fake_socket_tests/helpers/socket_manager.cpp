// Copyright (C) 2025 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "socket_manager.hpp"
#include "test_logging.hpp"

std::string connection_name(std::string const& _from, std::string const& _to) {
    return _from + "_to_" + _to;
}

#define LOCAL_LOG TEST_LOG << "[socket-manager] "

namespace vsomeip_v3::testing {

void socket_manager::add(std::string const& app) {
    auto const lock = std::scoped_lock(mtx_);
    name_to_context_[app] = nullptr;
}

void socket_manager::clear_handler(std::string const& app) {
    std::vector<std::shared_ptr<fake_tcp_socket_handle>> handle_to_clear;
    std::vector<std::shared_ptr<fake_tcp_acceptor_handle>> acceptor_handle_to_clear;
    // Destroy the io_stop_spy timer outside the lock: its destructor calls
    // clear_handler again, which would otherwise double-lock mtx_.
    std::unique_ptr<boost::asio::steady_timer> timer_to_destroy;
    {
        auto const lock = std::scoped_lock(mtx_);
        if (auto const it = timers_.find(app); it != timers_.end()) {
            timer_to_destroy = std::move(it->second);
            timers_.erase(it);
        }

        auto const it_context = name_to_context_.find(app);
        if (it_context == name_to_context_.end()) {
            // Fall through to destroy any moved timer after unlock.
        } else {
            LOCAL_LOG << "clearing handler for io_context: " << it_context->second;
            auto* io = it_context->second;
            context_to_name_.erase(io);
            name_to_context_.erase(app);

            auto const it_fds = context_to_fd_.find(io);
            if (it_fds != context_to_fd_.end()) {
                for (auto fd : it_fds->second) {
                    if (auto const it_handle = fd_to_handle_.find(fd); it_handle != fd_to_handle_.end()) {
                        if (auto handle = it_handle->second.lock(); handle) {
                            handle_to_clear.push_back(handle);
                        }
                    } else if (auto const it_acc = fd_to_acceptor_states_.find(fd); it_acc != fd_to_acceptor_states_.end()) {
                        if (auto handle = it_acc->second.lock(); handle) {
                            acceptor_handle_to_clear.push_back(handle);
                        }
                    }
                }
                context_to_fd_.erase(io);
            }
        }
    }
    timer_to_destroy.reset();
    for (auto& handle : handle_to_clear) {
        handle->clear_handler();
    }
    for (auto& handle : acceptor_handle_to_clear) {
        handle->clear_handler();
    }
}

void socket_manager::add_socket(std::weak_ptr<fake_tcp_socket_handle> _state, boost::asio::io_context* _io) {
    if (auto const state = _state.lock(); state) {
        auto fd = next_fd_++;
        if (next_fd_.load() < fd) {
            throw std::runtime_error("Exhausted fake file descriptors");
        }
        state->init(fd, weak_from_this());
        std::string app_name;
        std::string app_to_arm;
        {
            auto const lock = std::scoped_lock(mtx_);
            fd_to_handle_[fd] = _state;
            app_to_arm = try_add(_io, fd, "socket");
            if (auto const it_name = context_to_name_.find(_io); it_name != context_to_name_.end()) {
                app_name = it_name->second;
            }
        }
        if (!app_to_arm.empty()) {
            arm_io_stop_spy(app_to_arm, _io);
        }
        state->set_app_name(app_name);
    }
}

void socket_manager::remove(fd_t fd) {
    auto const lock = std::scoped_lock(mtx_);
    fd_to_handle_.erase(fd);
}

void socket_manager::add_acceptor(std::weak_ptr<fake_tcp_acceptor_handle> _state, boost::asio::io_context* _io) {
    if (auto const state = _state.lock(); state) {
        auto fd = next_fd_++;
        if (next_fd_.load() < fd) {
            throw std::runtime_error("Exhausted fake file descriptors");
        }
        state->init(fd, weak_from_this());
        std::string app_name;
        std::string app_to_arm;
        {
            auto const lock = std::scoped_lock(mtx_);
            fd_to_acceptor_states_[fd] = _state;
            app_to_arm = try_add(_io, fd, "acceptor");
            if (auto const it_name = context_to_name_.find(_io); it_name != context_to_name_.end()) {
                app_name = it_name->second;
            }
        }
        if (!app_to_arm.empty()) {
            arm_io_stop_spy(app_to_arm, _io);
        }
        state->set_app_name(app_name);
    }
}

/**
 * Notice that the d'tor will be called when boost::asio::io_context is destroyed,
 * as a consequence of cleaning up all handlers. Upon destruction of this handler,
 * the d'tor will ensure that the stored handlers are deleted, that would otherwise
 * have been destroyed during the destruction of io_context in the production code.
 * Note that this is necessary as long as the receive sockets/connections are
 * managed by themself.
 */
struct io_stop_spy {
    io_stop_spy(std::weak_ptr<socket_manager> _sm, std::string _app_name) :
        sm_(std::move(_sm)), app_name_(std::move(_app_name)), armed_(std::make_shared<bool>(true)) { }
    io_stop_spy(io_stop_spy const&) = delete;
    io_stop_spy& operator=(io_stop_spy const&) = delete;

    void disarm() {
        if (armed_) {
            *armed_ = false;
        }
    }

    ~io_stop_spy() {
        if (armed_ && *armed_) {
            if (auto sm = sm_.lock(); sm) {
                LOCAL_LOG << "io_spy deleted, calling clear handler for: " << app_name_;
                sm->clear_handler(app_name_);
            }
        }
    }
    std::weak_ptr<socket_manager> sm_;
    std::string app_name_;
    // Shared so a caller can disarm after the spy is moved into an asio handler.
    std::shared_ptr<bool> armed_;
};

std::string socket_manager::try_add(boost::asio::io_context* _io, fd_t _fd, char const* _type) {
    context_to_fd_[_io].push_back(_fd);
    if (auto const it = context_to_name_.find(_io); it != context_to_name_.end()) {
        LOCAL_LOG << "added fake fd: " << _fd << " (" << _type << ") to client: " << it->second << " with context: " << _io;
        return {};
    }

    for (auto& pair : name_to_context_) {
        if (!pair.second) {
            LOCAL_LOG << "connected: \"" << pair.first << "\" with io: " << _io;
            pair.second = _io;
            assignment_cv_.notify_all();
            context_to_name_[_io] = pair.first;
            LOCAL_LOG << "added fake fd: " << _fd << " (" << _type << ") to client: " << pair.first;

            // Defer timer arming to the caller (after mtx_ is released). Never
            // call async_wait under the lock: replacing/cancelling a handler
            // destroys io_stop_spy, which re-enters clear_handler.
            if (timers_.find(pair.first) == timers_.end()) {
                return pair.first;
            }
            return {};
        }
    }
    return {};
}

void socket_manager::arm_io_stop_spy(std::string const& _app, boost::asio::io_context* _io) {
    auto spy = std::make_unique<io_stop_spy>(weak_from_this(), _app);
    auto armed = spy->armed_;

    // Build the timer without holding mtx_: expires_at/async_wait must not run
    // while the socket_manager lock is held.
    auto timer = std::make_unique<boost::asio::steady_timer>(*_io);
    // ensure the timer does not really expire by waiting for the maximum time.
    // This leads to a handler destruction in the clean-up of the io_context, which
    // we need to know about to clean-up the handlers we stored in the fake_sockets,
    // belonging to this very io_context
    timer->expires_at(boost::asio::steady_timer::time_point::max());
    timer->async_wait([spy = std::move(spy)](auto ec) {
        LOCAL_LOG << "[ERROR] io_spy timer expired. Reporting ec: " << ec.message() << " for app: " << spy->app_name_;
    });

    {
        auto const lock = std::scoped_lock(mtx_);
        // Another thread may have armed or cleared this app meanwhile.
        if (timers_.find(_app) != timers_.end() || name_to_context_.find(_app) == name_to_context_.end()) {
            *armed = false; // abandoning: do not clear_handler on timer destruction
            return;
        }
        timers_.emplace(_app, std::move(timer));
    }
}

void socket_manager::remove_acceptor(fd_t _fd, boost::asio::ip::tcp::endpoint _ep) {
    auto const lock = std::scoped_lock(mtx_);
    fd_to_acceptor_states_.erase(_fd);
    ep_to_acceptor_states_.erase(_ep);
}

[[nodiscard]] bool socket_manager::bind_acceptor(boost::asio::ip::tcp::endpoint const& _ep,
                                                 std::weak_ptr<fake_tcp_acceptor_handle> _state) {
    auto const lock = std::scoped_lock(mtx_);
    if (auto const it = ep_to_acceptor_states_.find(_ep); it != ep_to_acceptor_states_.end()) {
        return false;
    }
    ep_to_acceptor_states_[_ep] = _state;
    return true;
}

[[nodiscard]] bool socket_manager::bind_socket(fake_tcp_socket_handle const& _handle) {
    auto const lock = std::scoped_lock(mtx_);
    return fail_on_bind_.count(_handle.get_app_name()) == 0;
}

void socket_manager::connect(boost::asio::ip::tcp::endpoint const& _ep, fake_tcp_socket_handle& _connecting, connect_handler _handler) {
    std::optional<boost::system::error_code> early_error;
    auto acceptor = [&]() -> std::shared_ptr<fake_tcp_acceptor_handle> {
        auto const lock = std::scoped_lock(mtx_);
        auto const it = ep_to_acceptor_states_.find(_ep);
        if (it == ep_to_acceptor_states_.end()) {
            early_error = boost::asio::error::make_error_code(boost::asio::error::host_unreachable);
            return nullptr;
        }
        auto acc = it->second.lock();
        if (!acc) {
            early_error = boost::asio::error::make_error_code(boost::asio::error::host_unreachable);
            return nullptr;
        }
        auto acc_name = acc->get_app_name();
        if (auto const it = app_to_next_connection_errors_.find(acc_name); it != app_to_next_connection_errors_.end()) {
            if (auto& err = it->second; !err.empty()) {
                early_error = *err.begin();
                err.erase(err.begin());
                return nullptr;
            }
        }
        if (auto const it = app_name_to_ignore_connections_count_.find(acc_name); it != app_name_to_ignore_connections_count_.end()) {
            if (it->second != 0) {
                --(it->second);
                // ignore the handler
                return nullptr;
            }
        }
        if (connections_to_ignore_.count(acc_name) > 0) {
            return nullptr;
        }
        return acc;
    }();
    if (early_error) {
        // _handler already re-posts onto the connecting socket's io_context.
        _handler(*early_error);
        return;
    }
    if (!acceptor) {
        return;
    }

    auto accepting = acceptor->connect(_connecting, std::move(_handler));
    if (!accepting) {
        // handler has been moved out
        return;
    }
    auto c_name = _connecting.get_app_name();
    auto s_name = accepting->get_app_name();
    if (!c_name.empty() && !s_name.empty()) {
        auto cn = connection_name(c_name, s_name);
        auto const lock = std::scoped_lock(mtx_);
        app_names_to_connection[cn] = std::pair(_connecting.weak_from_this(), accepting);
        ++connection_name_to_connection_count_[cn];
        connection_cv_.notify_all();
        LOCAL_LOG << "added: " << cn << " to the known connections";
    } else {
        LOCAL_LOG << "Error: socket encountered without set app name! "
                  << "c_name: " << c_name << ", s_name: " << s_name;
    }
}

[[nodiscard]] bool socket_manager::disconnect(std::string const& _from_name, std::optional<boost::system::error_code> _from_error,
                                              std::string const& _to_name, std::optional<boost::system::error_code> _to_error,
                                              socket_role _side_to_disconnect) {
    auto connection = get_connection(_from_name, _to_name);
    auto from = connection.first.lock();
    auto to = connection.second.lock();

    if (!from && !to) {
        auto const cn = connection_name(_from_name, _to_name);
        auto const lock = std::scoped_lock(mtx_);
        if (app_names_to_connection.find(cn) == app_names_to_connection.end()) {
            if (_from_error || _to_error) {
                LOCAL_LOG << "no connection " << cn << " to inject into";
                return false;
            }
            return true;
        }
    }

    auto inject = [&](std::shared_ptr<fake_tcp_socket_handle> const& _socket, std::optional<boost::system::error_code> const& _error,
                      std::string const& _name) {
        if (_socket) {
            _socket->disconnect(_error);
            return;
        }
        if (_error) {
            // Already closed: receive handler was invoked by shutdown/inner_close.
            LOCAL_LOG << "The error code: \"" << _error->message() << "\" not injected into \"" << _name
                      << "\"; socket already gone (handler already ran)";
        }
    };

    switch (_side_to_disconnect) {
    case socket_role::receiver:
        inject(to, _to_error, _to_name);
        break;
    case socket_role::sender:
        inject(from, _from_error, _from_name);
        break;
    case socket_role::unspecified:
    default:
        inject(from, _from_error, _from_name);
        inject(to, _to_error, _to_name);
        break;
    }

    return true;
}

[[nodiscard]] bool socket_manager::await_assignment(std::string const& _app, std::chrono::milliseconds _timeout) {
    auto lock = std::unique_lock(cv_mtx_);
    return assignment_cv_.wait_for(lock, _timeout, [&, this] {
        auto const data_lock = std::scoped_lock(mtx_);
        auto const it = name_to_context_.find(_app);
        return it != name_to_context_.end() && it->second != nullptr;
    });
}
void socket_manager::fail_on_bind(std::string const& _app, bool fail) {
    auto lock = std::unique_lock(mtx_);
    if (fail) {
        fail_on_bind_.insert(_app);
    } else {
        fail_on_bind_.erase(_app);
    }
}

[[nodiscard]] bool socket_manager::await_connectable(std::string const& _app, std::chrono::milliseconds _timeout) {
    auto lock = std::unique_lock(cv_mtx_);
    return connectable_cv_.wait_for(lock, _timeout, [&, this] {
        std::vector<std::shared_ptr<fake_tcp_acceptor_handle>> acceptors;
        {
            auto const data_lock = std::scoped_lock(mtx_);
            acceptors.reserve(ep_to_acceptor_states_.size());
            for (auto const& ep_ac : ep_to_acceptor_states_) {
                if (auto const acceptor = ep_ac.second.lock(); acceptor) {
                    acceptors.push_back(acceptor);
                }
            }
        }
        for (auto const& acceptor : acceptors) {
            if (acceptor->get_app_name() == _app) {
                return acceptor->is_awaiting_connection();
            }
        }
        return false;
    });
}

[[nodiscard]] bool socket_manager::await_connection(std::string const& _from, std::string const& _to, std::chrono::milliseconds _timeout) {
    auto lock = std::unique_lock(cv_mtx_);
    auto const cn = connection_name(_from, _to);
    return connection_cv_.wait_for(lock, _timeout, [&, this] {
        std::weak_ptr<fake_tcp_socket_handle> weak_from;
        std::weak_ptr<fake_tcp_socket_handle> weak_to;
        {
            auto const data_lock = std::scoped_lock(mtx_);
            auto const it = app_names_to_connection.find(cn);
            if (it == app_names_to_connection.end()) {
                return false;
            }
            weak_from = it->second.first;
            weak_to = it->second.second;
        }
        auto const from = weak_from.lock();
        if (!from) {
            return false;
        }
        return from->is_connected(weak_to);
    });
}

void socket_manager::awaiting() {
    connectable_cv_.notify_all();
}

size_t socket_manager::count_established_connections(std::string const& _from, std::string const& _to) {
    auto const lock = std::scoped_lock(mtx_);
    auto const it = connection_name_to_connection_count_.find(connection_name(_from, _to));
    return it == connection_name_to_connection_count_.end() ? 0 : it->second;
}

void socket_manager::report_on_connect(std::string const& _app_name, std::vector<boost::system::error_code> _next_errors) {
    auto const lock = std::scoped_lock(mtx_);
    auto& errors = app_to_next_connection_errors_[_app_name];
    errors.reserve(errors.size() + _next_errors.size());
    std::move(_next_errors.begin(), _next_errors.end(), std::back_inserter(errors));
}

void socket_manager::ignore_connections(std::string const& _app_name, size_t _number_of_ignored_connections) {
    auto const lock = std::scoped_lock(mtx_);
    app_name_to_ignore_connections_count_[_app_name] = _number_of_ignored_connections;
}

void socket_manager::set_ignore_connections(std::string const& _app_name, bool _ignore_connections) {
    auto const lock = std::scoped_lock(mtx_);
    if (_ignore_connections) {
        connections_to_ignore_.insert(_app_name);
    } else {
        connections_to_ignore_.erase(_app_name);
    }
}
[[nodiscard]] bool socket_manager::delay_message_processing(std::string const& _from, std::string const& _to, bool _delay) {
    auto [weak_from, weak_to] = get_connection(_from, _to);
    if (auto to = weak_to.lock(); to) {
        to->delay_processing(_delay);
        return true;
    }
    return false;
}

[[nodiscard]] bool socket_manager::set_ignore_inner_close(std::string const& _from, bool _ignore_in_from, std::string const& _to,
                                                          bool _ignore_in_to) {
    auto [weak_from, weak_to] = get_connection(_from, _to);

    bool ret = true;
    if (_ignore_in_from) {
        if (auto from = weak_from.lock(); from) {
            from->ignore_inner_close();
        } else {
            ret = false;
        }
    }
    if (_ignore_in_to) {
        if (auto to = weak_to.lock(); to) {
            to->ignore_inner_close();
        } else {
            ret = false;
        }
    }
    return ret;
}

[[nodiscard]] bool socket_manager::block_on_close_for(std::string const& _from, std::optional<std::chrono::milliseconds> _from_block_time,
                                                      std::string const& _to, std::optional<std::chrono::milliseconds> _to_block_time) {
    auto const lock = std::scoped_lock(mtx_);
    auto const cn = connection_name(_from, _to);
    auto const it_connection = app_names_to_connection.find(cn);
    if (it_connection == app_names_to_connection.end()) {
        return false;
    }
    bool ret = true;
    if (auto from = it_connection->second.first.lock(); from) {
        from->block_on_close_for(_from_block_time);
    } else {
        ret = false;
    }
    if (auto to = it_connection->second.second.lock(); to) {
        to->block_on_close_for(_to_block_time);
    } else {
        ret = false;
    }
    return ret;
}

void socket_manager::clear_command_record(std::string const& _from, std::string const& _to) {
    auto [weak_from, weak_to] = get_connection(_from, _to);
    if (auto to = weak_to.lock(); to) {
        to->received_command_record_.clear();
    }
}

[[nodiscard]] bool socket_manager::wait_for_command(std::string const& _from, std::string const& _to, protocol::id_e _id,
                                                    std::chrono::milliseconds _timeout) {
    auto [weak_from, weak_to] = get_connection(_from, _to);
    if (auto to = weak_to.lock(); to) {
        return to->received_command_record_.wait_for(_id, _timeout);
    }
    return false;
}

void socket_manager::set_ignore_broken_pipe(std::string const& _app_name, bool _set) {
    auto const lock = std::scoped_lock(mtx_);
    if (_set) {
        ignore_broken_pipe_.insert(_app_name);
    } else {
        ignore_broken_pipe_.erase(_app_name);
    }
}

bool socket_manager::ignore_broken_pipe(fake_tcp_socket_handle const& _handle) {
    auto const lock = std::scoped_lock(mtx_);
    auto app_name = _handle.get_app_name();
    auto const it = ignore_broken_pipe_.find(app_name);
    if (it == ignore_broken_pipe_.end()) {
        return false;
    }

    LOCAL_LOG << "ignoring broken_pipe for app: " << app_name;
    return true;
}

std::pair<std::weak_ptr<fake_tcp_socket_handle>, std::weak_ptr<fake_tcp_socket_handle>>
socket_manager::get_connection(std::string const& _from, std::string const& _to) {

    auto const cn = connection_name(_from, _to);
    auto const lock = std::scoped_lock(mtx_);
    auto const it_connection = app_names_to_connection.find(cn);
    if (it_connection == app_names_to_connection.end()) {
        return {};
    }
    return it_connection->second;
}
}
