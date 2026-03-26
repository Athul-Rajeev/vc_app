#pragma once
#include "Network/INetworkProvider.hpp"
#include <vector>
#include <string>
#include <cstdint>

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    void setProvider(INetworkProvider* networkProvider);
    void sendAudioPacket(const std::string& targetIp, const std::vector<uint8_t>& packetData);
    NetworkPacket receiveAudioPacket();

    std::string sendSynchronousTcp(const std::string& targetIp, const std::string& payload);
    void pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler);

private:
    INetworkProvider* m_activeProvider;
};