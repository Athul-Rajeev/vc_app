#pragma once
#include "Network/INetworkProvider.hpp"
#include "Network/NetworkManager.hpp"
#include "Audio/AudioEngine.hpp"
#include <memory>
#include <string>

class Application
{
public:
    Application();
    ~Application();

    bool initialize();
    void runMainLoop(const std::string& targetIp);

private:
    std::unique_ptr<INetworkProvider> m_networkProvider;
    NetworkManager m_networkManager;
    AudioEngine m_audioEngine;
    bool m_isRunning;
};