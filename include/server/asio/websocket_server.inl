/*!
    \file websocket_server.inl
    \brief WebSocket server inline implementation
    \author Ivan Shynkarenka
    \date 06.01.2016
    \copyright MIT License
*/

namespace CppServer {
namespace Asio {

template <class TServer, class TSession>
inline WebSocketServer<TServer, TSession>::WebSocketServer(std::shared_ptr<Service> service, InternetProtocol protocol, int port)
    : _service(service),
      _acceptor(_service->service()),
      _socket(_service->service()),
      _started(false)
{
    switch (protocol)
    {
        case InternetProtocol::IPv4:
            _endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
            break;
        case InternetProtocol::IPv6:
            _endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v6(), port);
            break;
    }
}

template <class TServer, class TSession>
inline WebSocketServer<TServer, TSession>::WebSocketServer(std::shared_ptr<Service> service, const std::string& address, int port)
    : _service(service),
      _acceptor(_service->service()),
      _socket(_service->service()),
      _started(false)
{
    _endpoint = asio::ip::tcp::endpoint(asio::ip::address::from_string(address), port);
}

template <class TServer, class TSession>
inline WebSocketServer<TServer, TSession>::WebSocketServer(std::shared_ptr<Service> service, const asio::ip::tcp::endpoint& endpoint)
    : _service(service),
      _endpoint(endpoint),
      _acceptor(_service->service()),
      _socket(_service->service()),
      _started(false)
{
}

template <class TServer, class TSession>
inline bool WebSocketServer<TServer, TSession>::Start()
{
    if (!_service->IsStarted())
        return false;

    if (IsStarted())
        return false;

    // Post the start routine
    auto self(this->shared_from_this());
    _service->service().post([this, self]()
    {
        // Create the server acceptor
        _acceptor = asio::ip::tcp::acceptor(_service->service(), _endpoint);

        // Update the started flag
        _started = true;

        // Call the server started handler
        onStarted();

        // Perform the first server accept
        Accept();
    });

    return true;
}

template <class TServer, class TSession>
inline bool WebSocketServer<TServer, TSession>::Stop()
{
    if (!IsStarted())
        return false;

    // Post the stopped routine
    auto self(this->shared_from_this());
    _service->service().post([this, self]()
    {
        // Disconnect all sessions
        DisconnectAll();

        // Close the server acceptor
        _acceptor.close();

        // Clear multicast buffer
        ClearBuffers();

        // Update the started flag
        _started = false;

        // Call the server stopped handler
        onStopped();
    });

    return true;
}

template <class TServer, class TSession>
inline bool WebSocketServer<TServer, TSession>::Restart()
{
    if (!Stop())
        return false;

    while (IsStarted())
        CppCommon::Thread::Yield();

    return Start();
}

template <class TServer, class TSession>
inline bool WebSocketServer<TServer, TSession>::Multicast(const void* buffer, size_t size)
{
    if (!IsStarted())
        return false;

    std::lock_guard<std::mutex> locker(_multicast_lock);

    const uint8_t* bytes = (const uint8_t*)buffer;
    _multicast_buffer.insert(_multicast_buffer.end(), bytes, bytes + size);

    // Dispatch the multicast routine
    auto self(this->shared_from_this());
    _service->Dispatch([this, self]()
    {
        // Multicast all sessions
        for (auto& session : _sessions)
            session.second->Send(_multicast_buffer.data(), _multicast_buffer.size());

        // Clear the multicast buffer
        _multicast_buffer.clear();
    });

    return true;
}

template <class TServer, class TSession>
inline bool WebSocketServer<TServer, TSession>::DisconnectAll()
{
    if (!IsStarted())
        return false;

    // Dispatch the disconnect routine
    auto self(this->shared_from_this());
    _service->Dispatch([this, self]()
    {
        // Disconnect all sessions
        for (auto& session : _sessions)
            session.second->Disconnect();
    });

    return true;
}

template <class TServer, class TSession>
inline std::shared_ptr<TSession> WebSocketServer<TServer, TSession>::RegisterSession()
{
    // Create and register a new session
    auto self(this->shared_from_this());
    auto session = std::make_shared<TSession>(self, std::move(_socket));
    _sessions.emplace(session->id(), session);

    // Connect a new session
    session->Connect();

    // Call a new session connected handler
    onConnected(session);

    return session;
}

template <class TServer, class TSession>
inline void WebSocketServer<TServer, TSession>::UnregisterSession(const CppCommon::UUID& id)
{
    // Try to find the unregistered session
    auto it = _sessions.find(id);
    if (it != _sessions.end())
    {
        // Call the session disconnected handler
        onDisconnected(it->second);

        // Erase the session
        _sessions.erase(it);
    }
}

template <class TServer, class TSession>
inline void WebSocketServer<TServer, TSession>::ClearBuffers()
{
    std::lock_guard<std::mutex> locker(_multicast_lock);
    _multicast_buffer.clear();
}

} // namespace Asio
} // namespace CppServer
