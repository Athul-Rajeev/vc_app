#pragma once
#include "Network/INetworkProvider.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

#define ASIO_STANDALONE
#include <asio.hpp>

class TailscaleNetwork : public INetworkProvider
{
public:
    TailscaleNetwork();
    ~TailscaleNetwork() override;

    bool initialize(bool isServerMode) override;
    void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) override;
    NetworkPacket receiveData() override;

    std::string sendSynchronousTcp(const std::string& targetIp, const std::string& payload) override;
    void pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler) override;
    
    void waitForEvents(int timeoutMs) override;

    int getLocalTcpPort() override;
    int getLocalUdpPort() override;

private:
    asio::io_context m_ioContext;
    asio::ip::udp::socket m_udpSocket;
    asio::ip::tcp::acceptor m_tcpAcceptor;
    int m_port;
    bool m_isServerMode;
};