#pragma once
#include "Network/INetworkProvider.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    void setProvider(INetworkProvider* networkProvider);
    void sendAudioPacket(const std::string& targetIp, const std::vector<uint8_t>& packetData);
    NetworkPacket receiveAudioPacket();

    void pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler);

    int getLocalUdpPort();

    bool connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage);
    void sendPersistentTcp(const std::string& payload);
    void broadcastTcp(const std::string& payload);
    void sendTcpTo(const std::string& uuid, const std::string& payload);
private:
    INetworkProvider* m_activeProvider;
};