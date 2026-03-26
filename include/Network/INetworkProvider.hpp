#pragma once
#include <vector>
#include <string>
#include <cstdint>

#include <functional>

struct NetworkPacket
{
    std::string senderIp;
    std::vector<uint8_t> payload;
};

class INetworkProvider
{
public:
    virtual ~INetworkProvider() = default;
    
    virtual bool initialize(bool isServerMode) = 0;
    virtual void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) = 0;
    virtual NetworkPacket receiveData() = 0;

    virtual std::string sendSynchronousTcp(const std::string& targetIp, const std::string& payload) = 0;
    virtual void pollTcpConnections(std::function<std::string(const std::string& incomingIp, const std::string& payload)> requestHandler) = 0;
};