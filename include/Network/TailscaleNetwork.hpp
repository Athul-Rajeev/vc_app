#pragma once

#include "Network/INetworkProvider.hpp"
#include "Network/TcpSession.hpp"
#include <shared_mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <map>
#include <mutex>
#include <functional>

#define ASIO_STANDALONE
#include <asio.hpp>

class TailscaleNetwork : public INetworkProvider
{
public:
    TailscaleNetwork();
    ~TailscaleNetwork() override;

    bool initialize(bool isServerMode) override;
    
    void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) override;
    void sendData(const asio::ip::udp::endpoint& targetEndpoint, const std::vector<uint8_t>& dataPayload) override;
    
    void sendDataAsync(const std::string& targetIp, std::shared_ptr<std::vector<uint8_t>> dataPayload) override;
    void sendDataAsync(const asio::ip::udp::endpoint& targetEndpoint, std::shared_ptr<std::vector<uint8_t>> dataPayload) override;
    
    void setUdpReceiveCallback(std::function<void(const asio::ip::udp::endpoint&, const uint8_t*, size_t)> callback) override;

    void pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler) override;

    int getLocalUdpPort() override;
    bool connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage) override;
    void sendPersistentTcp(const std::string& payload) override;
    void broadcastTcp(const std::string& payload) override;
    void sendTcpTo(const std::string& uuid, const std::string& payload) override;

private:
    void startTcpAcceptor(std::function<std::string(const std::string&, const std::string&)> requestHandler);
    void startUdpReceiveLoop();

    asio::io_context m_tcpContext;
    asio::io_context m_udpContext;
    
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_tcpWorkGuard;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_udpWorkGuard;

    std::thread m_tcpThread;
    std::thread m_udpThread;

    asio::ip::udp::socket m_udpSocket;
    asio::ip::tcp::acceptor m_tcpAcceptor;
    asio::ip::udp::endpoint m_remoteEndpoint;
    
    std::mutex m_sessionMutex;
    std::map<std::string, std::shared_ptr<TcpSession>> m_activeSessions;

    std::unordered_map<std::string, asio::ip::udp::endpoint> m_endpointCache;
    std::shared_mutex m_endpointCacheMutex;
    std::vector<uint8_t> m_receiveBuffer;
    int m_port;
    bool m_isServerMode;
    std::shared_ptr<TcpSession> m_clientSession;
    
    std::function<void(const asio::ip::udp::endpoint&, const uint8_t*, size_t)> m_udpReceiveCallback;
};