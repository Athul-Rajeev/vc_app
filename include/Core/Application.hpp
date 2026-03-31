#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <map>
#include "Network/INetworkProvider.hpp"
#include "Network/NetworkManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "UI/WindowManager.hpp"
#include "Core/ChannelState.hpp"
#include "Database/DatabaseManager.hpp"
#include "Utils/Utils.hpp"
#include "Network/TailscaleNetwork.hpp"
#include "Utils/LockFreeQueue.hpp"

struct PeerRoutingState
{
    char uuid[36];
    char endpoint[64];
    int activeChannelId;
};

class Application
{
public:
    Application();
    ~Application();

    bool initialize(bool isServerMode);
    void runMainLoop(const std::string& targetIp);

private:
    void processClientTcpPush(const std::string& payload);
    void serverControlLoop();
    void serverRouterLoop();
    void clientControlLoop(const std::string& serverIp);
    void clientAudioLoop(const std::string& serverIp);

    std::thread m_controlThread;
    std::thread m_routerThread;

    std::atomic<int> m_activeVoiceChannelId{-1};
    std::atomic<bool> m_isMuted{false};
    std::atomic<bool> m_isDeafened{false};

    LockFreeQueue<PeerRoutingState, 256> m_routingQueue;

    std::unique_ptr<INetworkProvider> m_networkProvider;
    NetworkManager m_networkManager;
    std::unique_ptr<DatabaseManager> m_dbManager;
    AudioEngine m_audioEngine;
    WindowManager m_windowManager;

    ChannelState m_textChannelState;
    ChannelState m_voiceChannelState;

    std::atomic<bool> m_isRunning;
    bool m_isServerMode;


};