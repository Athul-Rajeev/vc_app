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

std::string NetworkManager::sendSynchronousTcp(const std::string& targetIp, const std::string& payload)
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->sendSynchronousTcp(targetIp, payload);
    }
    return "";
}

void NetworkManager::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->pollTcpConnections(requestHandler);
    }
}

void NetworkManager::waitForEvents(int timeoutMs)
{
    if (m_activeProvider != nullptr)
    {
        m_activeProvider->waitForEvents(timeoutMs);
    }
}

int NetworkManager::getLocalTcpPort()
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->getLocalTcpPort();
    }
    return 0;
}

int NetworkManager::getLocalUdpPort()
{
    if (m_activeProvider != nullptr)
    {
        return m_activeProvider->getLocalUdpPort();
    }
    return 0;
}