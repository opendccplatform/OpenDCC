// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/ipc_commands_api/server.h"

#include "opendcc/base/ipc_commands_api/command_registry.h"
#include "opendcc/base/ipc_commands_api/server_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/utils/process.h"

#include "opendcc/base/defines.h"
#include "opendcc/base/vendor/cppzmq/zmq.hpp"

#include <queue>
#include <mutex>
#include <future>
#include <iostream>
#include <condition_variable>

OPENDCC_NAMESPACE_OPEN

namespace base::ipc
{

    //////////////////////////////////////////////////////////////////////////
    // CommandServerImpl
    //////////////////////////////////////////////////////////////////////////

    class CommandServerImpl
    {
    public:
        CommandServerImpl(const ServerInfo& info)
            : m_info(info)
            , m_pid(utils::get_pid_string())
            , m_stop_listen_future(m_stop_listen_signal.get_future())
            , m_listener(m_context, zmq::socket_type::pull)
        {
            if (!valid())
            {
                m_info = {};
                return;
            }

            const auto fixed_info = select_free_port(m_info);
            m_info.hostname = fixed_info.hostname;
            m_info.input_port = fixed_info.input_port;
            if (!fixed_info.valid())
            {
                m_info = {};
                return;
            }

            try
            {
                m_listener.connect(m_info.get_tcp_address());
                m_listener.set(zmq::sockopt::sndtimeo, CommandServer::get_server_timeout());
                m_listener.set(zmq::sockopt::rcvtimeo, CommandServer::get_server_timeout());
            }
            catch (const zmq::error_t& error)
            {
                const auto what = error.what();
                OPENDCC_ERROR("CommandServer::Constructor: {}", what);
                m_info = {};
                return;
            }

            m_input_thread = std::thread([this] { listen_commands(); });
            m_send_thread = std::thread([this] { send_commands(); });
        }

        ~CommandServerImpl()
        {
            // https://stackoverflow.com/questions/55743983/zeromq-is-blocked-in-context-close-how-to-safely-close-socket-and-context-in
            // https://github.com/zeromq/libzmq/issues/2312

            m_context.shutdown();

            if (m_input_thread.joinable())
            {
                m_stop_listen_signal.set_value();

                m_input_thread.join();
            }

            if (m_send_thread.joinable())
            {
                {
                    std::unique_lock<std::mutex> lock(m_send_mutex);
                    m_stop_send = true;
                }

                m_send_cv.notify_all();

                m_send_thread.join();
            }
        }

        void send_command(const ServerInfo& info, const Command& command)
        {
            if (!valid())
            {
                return;
            }

            {
                SendQueueLocker lock(m_send_mutex);

                m_send_queue.push({ info, command });
            }

            m_send_cv.notify_all();
        }

        void send_command(const std::string& pid, const Command& command)
        {
            const auto info = ServerRegistry::instance().find_server(pid);
            send_command(info, command);
        }

        const ServerInfo& get_info() const { return m_info; }

        bool valid() const { return m_info.valid(); }

    private:
        ServerInfo select_free_port(const ServerInfo& info)
        {
            try
            {
                // https://stackoverflow.com/questions/16699890/connect-to-first-free-port-with-tcp-using-0mq

                auto address = info.get_tcp_address();
                zmq::socket_t socket(m_context, zmq::socket_type::pull);
                socket.bind(address);

                address = socket.get(zmq::sockopt::last_endpoint);
                socket.unbind(address);

                return ServerInfo::from_string(address);
            }
            catch (const zmq::error_t& error)
            {
                OPENDCC_ERROR("CommandServer::select_free_port: {}", error.what());
                return ServerInfo();
            }
        }

        void listen_commands()
        {
            const auto wait = std::chrono::milliseconds(0);

            zmq::message_t message;
            while (m_stop_listen_future.wait_for(wait) != std::future_status::ready)
            {
                try
                {
                    if (!m_listener.recv(&message))
                    {
                        continue;
                    }

                    const auto message_str = message.to_string();
                    const auto command = Command::from_string(message_str);

                    CommandRegistry::instance().handle_command(command);
                }
                catch (const zmq::error_t& error)
                {
                    if (error.num() != ETERM)
                    {
                        OPENDCC_ERROR("CommandServer::listen_commands: {}", error.what());
                    }
                }
            }
        }

        void send_commands()
        {
            const auto wait = std::chrono::milliseconds(0);
            auto& registry = ServerRegistry::instance();

            SendQueue send_queue;
            while (true)
            {
                {
                    std::unique_lock<std::mutex> lock(m_send_mutex);
                    m_send_cv.wait(lock, [this, wait] { return m_stop_send || !m_send_queue.empty(); });
                    std::swap(send_queue, m_send_queue);

                    if (m_stop_send)
                    {
                        break;
                    }
                }

                while (!send_queue.empty())
                {
                    const auto& front = send_queue.front();
                    const auto address = front.info.get_tcp_address();
                    const auto str_command = front.command.to_string();

                    try
                    {
                        zmq::socket_t socket(m_context, zmq::socket_type::push);
                        socket.set(zmq::sockopt::sndtimeo, CommandServer::get_server_timeout());
                        socket.set(zmq::sockopt::rcvtimeo, CommandServer::get_server_timeout());
                        socket.bind(address);
                        socket.send(str_command.begin(), str_command.end());
                        socket.unbind(address);
                        socket.close();
                    }
                    catch (const zmq::error_t& error)
                    {
                        OPENDCC_ERROR("CommandServer send_command to Server ({}) end with error: {}", address, error.what());

                        if (error.num() == EADDRINUSE) // Address in use
                        {
                            OPENDCC_DEBUG("CommandServer:Try send again.");
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }
                    }

                    send_queue.pop();
                }
            }
        }

        ServerInfo m_info;

        std::string m_pid;

        std::thread m_input_thread;
        std::thread m_send_thread;

        std::promise<void> m_stop_listen_signal;
        std::future<void> m_stop_listen_future;

        std::condition_variable m_send_cv;
        bool m_stop_send = false;

        zmq::context_t m_context;
        zmq::socket_t m_listener;

        struct SendInfo
        {
            ServerInfo info;
            Command command;
        };
        using SendQueue = std::queue<SendInfo>;
        using SendQueueLocker = std::lock_guard<std::mutex>;

        std::mutex m_send_mutex;
        SendQueue m_send_queue;
    };

    //////////////////////////////////////////////////////////////////////////
    // CommandServer
    //////////////////////////////////////////////////////////////////////////

    CommandServer::CommandServer(const ServerInfo& info)
    {
        m_impl = std::make_unique<CommandServerImpl>(info);
    }

    CommandServer::~CommandServer()
    {
        m_impl.reset();
    }

    void CommandServer::send_command(const ServerInfo& info, const Command& command)
    {
        m_impl->send_command(info, command);
    }

    void CommandServer::send_command(const std::string& pid, const Command& command)
    {
        m_impl->send_command(pid, command);
    }

    const ServerInfo& CommandServer::get_info() const
    {
        return m_impl->get_info();
    }

    bool CommandServer::valid() const
    {
        return m_impl->valid();
    }

    /* static */ int CommandServer::s_server_timeout = 1000;

    void CommandServer::set_server_timeout(const int server_timeout)
    {
        s_server_timeout = server_timeout;
    }

    int CommandServer::get_server_timeout()
    {
        return s_server_timeout;
    }

}

OPENDCC_NAMESPACE_CLOSE
