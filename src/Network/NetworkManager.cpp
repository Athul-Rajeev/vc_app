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

NetworkPacket NetworkManager::receiveAudioPacket()
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->receiveData();
    }
    
    return NetworkPacket{"", std::vector<uint8_t>()};
}

void NetworkManager::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->pollTcpConnections(requestHandler);
    }
}

int NetworkManager::getLocalUdpPort()
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->getLocalUdpPort();
    }
    return 0;
}

bool NetworkManager::connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage)
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->connectPersistentTcp(targetIp, onMessage);
    }
    return false;
}

void NetworkManager::sendPersistentTcp(const std::string& payload)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->sendPersistentTcp(payload);
    }
}

void NetworkManager::broadcastTcp(const std::string& payload)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->broadcastTcp(payload);
    }
}

void NetworkManager::sendTcpTo(const std::string& uuid, const std::string& payload)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->sendTcpTo(uuid, payload);
    }
}