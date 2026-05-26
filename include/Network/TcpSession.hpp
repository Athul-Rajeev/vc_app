#pragma once
#include <memory>
#include <string>
#include <deque>
#include <functional>

#define ASIO_STANDALONE
#include <asio.hpp>

class TcpSession : public std::enable_shared_from_this<TcpSession>
{
public:
    TcpSession(asio::ip::tcp::socket socket);
    ~TcpSession();

    void start(std::function<void(std::shared_ptr<TcpSession>, const std::string&)> onMessage,
               std::function<void(std::shared_ptr<TcpSession>)> onDisconnect);
    
    void sendData(const std::string& message);
    void closeSession();
    
    std::string getRemoteEndpoint() const;
    void setClientUuid(const std::string& uuid);
    std::string getClientUuid() const;
    void resetHeartbeat();

private:
    void readHeader();
    void readBody();
    void writeNext();
    void startHeartbeatTimer();

    asio::ip::tcp::socket m_socket;
    asio::steady_timer m_heartbeatTimer;
    asio::strand<asio::ip::tcp::socket::executor_type> m_strand;
    
    std::deque<std::string> m_outboxQueue;
    
    uint32_t m_inboundHeaderSize;
    std::vector<char> m_inboundBodyBuffer;
    std::string m_clientUuid;
    
    std::function<void(std::shared_ptr<TcpSession>, const std::string&)> m_onMessageCallback;
    std::function<void(std::shared_ptr<TcpSession>)> m_onDisconnectCallback;
    
    bool m_isWritingActive;
    bool m_isDead;
    bool m_hasReceivedHeartbeat;
};