#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

#define ASIO_STANDALONE
#include <asio.hpp>

struct NetworkPacket
{
    asio::ip::udp::endpoint senderEndpoint;
    std::vector<uint8_t> payload;
};

class INetworkProvider
{
public:
    virtual ~INetworkProvider() = default;
    
    virtual bool initialize(bool isServerMode) = 0;
    virtual void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) = 0;
    
    virtual bool receiveData(NetworkPacket& outPacket) = 0;

    virtual void sendData(const asio::ip::udp::endpoint& targetEndpoint, const std::vector<uint8_t>& dataPayload) = 0;

    virtual void pollTcpConnections(std::function<std::string(const std::string& incomingIp, const std::string& payload)> requestHandler) = 0;

    virtual int getLocalUdpPort() = 0;

    virtual bool connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage) = 0;
    virtual void sendPersistentTcp(const std::string& payload) = 0;
    virtual void broadcastTcp(const std::string& payload) = 0;
    virtual void sendTcpTo(const std::string& uuid, const std::string& payload) = 0;
};