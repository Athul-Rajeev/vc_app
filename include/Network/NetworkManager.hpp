#pragma once

#include "Network/INetworkProvider.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

class NetworkManager
{
public:
    void setProvider(INetworkProvider* provider);
    
    void sendAudioPacket(const std::string& targetIp, std::shared_ptr<std::vector<uint8_t>> payload);
    void sendAudioPacket(const asio::ip::udp::endpoint& targetEndpoint, std::shared_ptr<std::vector<uint8_t>> payload);
    
    void pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> handler);
    int getLocalUdpPort();

    bool connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage);
    void sendPersistentTcp(const std::string& payload);
    void broadcastTcp(const std::string& payload);
    void sendTcpTo(const std::string& uuid, const std::string& payload);

private:
    INetworkProvider* m_provider = nullptr;
};