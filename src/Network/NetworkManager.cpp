#include "Network/NetworkManager.hpp"

NetworkManager::NetworkManager()
{
    m_activeProvider = nullptr;
}

NetworkManager::~NetworkManager()
{
}

void NetworkManager::setProvider(INetworkProvider* networkProvider)
{
    m_activeProvider = networkProvider;
}

void NetworkManager::sendAudioPacket(const std::string& targetIp, const std::vector<uint8_t>& packetData)
{
    if (m_activeProvider != nullptr && !packetData.empty())
    {
        m_activeProvider->sendData(targetIp, packetData);
    }
}

std::vector<uint8_t> NetworkManager::receiveAudioPacket()
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->receiveData();
    }
    
    return std::vector<uint8_t>();
}