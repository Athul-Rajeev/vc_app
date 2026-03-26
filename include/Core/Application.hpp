#pragma once
#include "Network/INetworkProvider.hpp"
#include "Network/NetworkManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "UI/WindowManager.hpp"
#include <memory>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

class Application
{
public:
    Application();
    ~Application();

    bool initialize();
    void runMainLoop(const std::string& targetIp);

private:
    void networkThreadLoop(const std::string& targetIp);

    std::unique_ptr<INetworkProvider> m_networkProvider;
    NetworkManager m_networkManager;
    AudioEngine m_audioEngine;
    WindowManager m_windowManager;

    std::atomic<bool> m_isRunning;
    std::thread m_networkThread;
};