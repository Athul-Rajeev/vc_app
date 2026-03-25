#pragma once
#include <vector>
#include <string>

class ChannelState
{
public:
    ChannelState();
    ~ChannelState();

    void joinChannel(int channelId);
    void leaveCurrentChannel();
    std::vector<std::string> getActivePeers() const;

private:
    int m_currentChannelId;
    std::vector<std::string> m_activePeers;
    bool m_isConnected;
};