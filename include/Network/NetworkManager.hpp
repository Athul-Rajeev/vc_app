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
    std::vector<uint8_t> receiveAudioPacket();

private:
    INetworkProvider* m_activeProvider;
};