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

    bool initialize() override;
    void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) override;
    std::vector<uint8_t> receiveData() override;

private:
    asio::io_context m_ioContext;
    asio::ip::udp::socket m_udpSocket;
    int m_port;
};