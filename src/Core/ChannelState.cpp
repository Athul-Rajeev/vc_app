#include "Core/ChannelState.hpp"

ChannelState::ChannelState()
{
    m_currentChannelId = -1;
    m_isConnected = false;
}

ChannelState::~ChannelState()
{
}

void ChannelState::joinChannel(int channelId)
{
    m_currentChannelId = channelId;
    m_isConnected = true;
}

void ChannelState::leaveCurrentChannel()
{
    m_currentChannelId = -1;
    m_isConnected = false;
    m_activePeers.clear();
}

int ChannelState::getCurrentChannelId() const
{
    return m_currentChannelId;
}

std::vector<std::string> ChannelState::getActivePeers() const
{
    return m_activePeers;
}