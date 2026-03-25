#pragma once
#include <vector>
#include <string>
#include <cstdint>

class INetworkProvider
{
public:
    virtual ~INetworkProvider() = default;
    
    virtual bool initialize() = 0;
    virtual void sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload) = 0;
    virtual std::vector<uint8_t> receiveData() = 0;
};