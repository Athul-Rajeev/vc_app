#pragma once
#include "Network/INetworkProvider.hpp"
#include "Network/NetworkManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "UI/WindowManager.hpp"
#include "Core/ChannelState.hpp"
#include "Database/DatabaseManager.hpp"
#include <memory>
#include <string>
#include <thread>
#include <atomic>

class Application
{
public:
    Application();
    ~Application();

    bool initialize(bool isServerMode);
    void runMainLoop(const std::string& targetIp);

private:
    void clientThreadLoop(const std::string& serverIp);
    void serverThreadLoop();
    void processClientTcpPush(const std::string& payload);

    std::unique_ptr<INetworkProvider> m_networkProvider;
    NetworkManager m_networkManager;
    std::unique_ptr<DatabaseManager> m_dbManager;
    AudioEngine m_audioEngine;
    WindowManager m_windowManager;

    ChannelState m_textChannelState;
    ChannelState m_voiceChannelState;

    std::atomic<bool> m_isRunning;
    std::thread m_networkThread;
    bool m_isServerMode;
};