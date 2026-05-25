#include "Network/NetworkManager.hpp"
#include <spdlog/spdlog.h>

void NetworkManager::setProvider(INetworkProvider* provider)
{
    m_provider = provider;
}

void NetworkManager::sendAudioPacket(const std::string& targetIp, std::shared_ptr<std::vector<uint8_t>> payload)
{
    if (m_provider)
    {
        m_provider->sendDataAsync(targetIp, payload);
    }
}

void NetworkManager::sendAudioPacket(const asio::ip::udp::endpoint& targetEndpoint, std::shared_ptr<std::vector<uint8_t>> payload)
{
    if (m_provider)
    {
        m_provider->sendDataAsync(targetEndpoint, payload);
    }
}

void NetworkManager::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> handler)
{
    if (m_provider)
    {
        m_provider->pollTcpConnections(handler);
    }
}

int NetworkManager::getLocalUdpPort()
{
    if (m_provider)
    {
        return m_provider->getLocalUdpPort();
    }
    return 0;
}

bool NetworkManager::connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage)
{
    if (m_provider)
    {
        return m_provider->connectPersistentTcp(targetIp, onMessage);
    }
    return false;
}

void NetworkManager::sendPersistentTcp(const std::string& payload)
{
    if (m_provider)
    {
        m_provider->sendPersistentTcp(payload);
    }
}

void NetworkManager::broadcastTcp(const std::string& payload)
{
    if (m_provider)
    {
        m_provider->broadcastTcp(payload);
    }
}

void NetworkManager::sendTcpTo(const std::string& uuid, const std::string& payload)
{
    if (m_provider)
    {
        m_provider->sendTcpTo(uuid, payload);
    }
}