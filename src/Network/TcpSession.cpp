#include "Network/TcpSession.hpp"
#include <iostream>

TcpSession::TcpSession(asio::ip::tcp::socket socket)
    : m_socket(std::move(socket)), 
      m_heartbeatTimer(m_socket.get_executor()),
      m_strand(asio::make_strand(m_socket.get_executor())),
      m_inboundHeaderSize(0), 
      m_isWritingActive(false), 
      m_isDead(false), 
      m_hasReceivedHeartbeat(true)
{
}

TcpSession::~TcpSession()
{
    asio::error_code errorCode;
    m_heartbeatTimer.cancel(errorCode);
    
    if (m_socket.is_open())
    {
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, errorCode);
        m_socket.close(errorCode);
    }
}

void TcpSession::start(std::function<void(std::shared_ptr<TcpSession>, const std::string&)> onMessage, std::function<void(std::shared_ptr<TcpSession>)> onDisconnect)
{
    m_onMessageCallback = onMessage;
    m_onDisconnectCallback = onDisconnect;
    startHeartbeatTimer();
    readHeader();
}

void TcpSession::sendData(const std::string& message)
{
    auto self(shared_from_this());
    asio::post(m_strand, [this, self, message]()
    {
        if (m_isDead)
        {
            return;
        }

        uint32_t messageLength = static_cast<uint32_t>(message.size());
        std::string framedMessage;
        framedMessage.resize(sizeof(uint32_t) + messageLength);
        
        std::memcpy(&framedMessage[0], &messageLength, sizeof(uint32_t));
        std::memcpy(&framedMessage[sizeof(uint32_t)], message.data(), messageLength);

        bool writeInProgress = m_isWritingActive;
        m_outboxQueue.push_back(framedMessage);

        if (!writeInProgress)
        {
            writeNext();
        }
    });
}

void TcpSession::closeSession()
{
    auto self(shared_from_this());
    asio::post(m_strand, [this, self]()
    {
        if (!m_isDead)
        {
            m_isDead = true;
            asio::error_code errorCode;
            m_heartbeatTimer.cancel(errorCode);
            m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, errorCode);
            m_socket.close(errorCode);

            if (m_onDisconnectCallback)
            {
                m_onDisconnectCallback(self);
            }
        }
    });
}

std::string TcpSession::getRemoteEndpoint() const
{
    asio::error_code errorCode;
    auto remoteEndpoint = m_socket.remote_endpoint(errorCode);
    if (!errorCode)
    {
        return remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port());
    }
    return "Unknown";
}

void TcpSession::setClientUuid(const std::string& uuid)
{
    m_clientUuid = uuid;
}

std::string TcpSession::getClientUuid() const
{
    return m_clientUuid;
}

void TcpSession::resetHeartbeat()
{
    m_hasReceivedHeartbeat = true;
}

void TcpSession::readHeader()
{
    auto self(shared_from_this());
    asio::async_read(m_socket, asio::buffer(&m_inboundHeaderSize, sizeof(uint32_t)), asio::bind_executor(m_strand, [this, self](asio::error_code errorCode, std::size_t)
    {
        if (!errorCode && !m_isDead)
        {
            if (m_inboundHeaderSize > 0 && m_inboundHeaderSize < 1048576) // 1MB sanity check limits bad allocs
            {
                m_inboundBodyBuffer.resize(m_inboundHeaderSize);
                readBody();
            }
            else
            {
                closeSession();
            }
        }
        else
        {
            closeSession();
        }
    }));
}

void TcpSession::readBody()
{
    auto self(shared_from_this());
    asio::async_read(m_socket, asio::buffer(m_inboundBodyBuffer.data(), m_inboundHeaderSize), asio::bind_executor(m_strand, [this, self](asio::error_code errorCode, std::size_t)
    {
        if (!errorCode && !m_isDead)
        {
            std::string payload(m_inboundBodyBuffer.begin(), m_inboundBodyBuffer.end());
            m_hasReceivedHeartbeat = true; 

            if (m_onMessageCallback)
            {
                m_onMessageCallback(self, payload);
            }
            readHeader();
        }
        else
        {
            closeSession();
        }
    }));
}

void TcpSession::writeNext()
{
    auto self(shared_from_this());
    m_isWritingActive = true;
    
    asio::async_write(m_socket, asio::buffer(m_outboxQueue.front()), asio::bind_executor(m_strand, [this, self](asio::error_code errorCode, std::size_t)
    {
        if (!errorCode && !m_isDead)
        {
            m_outboxQueue.pop_front();
            if (!m_outboxQueue.empty())
            {
                writeNext();
            }
            else
            {
                m_isWritingActive = false;
            }
        }
        else
        {
            closeSession();
        }
    }));
}

void TcpSession::startHeartbeatTimer()
{
    auto self(shared_from_this());
    m_heartbeatTimer.expires_after(std::chrono::milliseconds(45000));
    m_heartbeatTimer.async_wait(asio::bind_executor(m_strand, [this, self](asio::error_code errorCode)
    {
        if (!errorCode && !m_isDead)
        {
            if (!m_hasReceivedHeartbeat)
            {
                std::cout << "Heartbeat timeout for session " << getRemoteEndpoint() << ". Purging." << std::endl;
                closeSession();
            }
            else
            {
                m_hasReceivedHeartbeat = false;
                startHeartbeatTimer();
            }
        }
    }));
}