#include "Core/Application.hpp"
#include "Network/TailscaleNetwork.hpp"
#include <iostream>
#include <thread>
#include <chrono>

Application::Application()
{
    m_isRunning = false;
    m_networkProvider = std::make_unique<TailscaleNetwork>();
}

Application::~Application()
{
}

bool Application::initialize()
{
    bool networkSuccess = m_networkProvider->initialize();
    if (!networkSuccess)
    {
        return false;
    }

    m_networkManager.setProvider(m_networkProvider.get());

    bool audioSuccess = m_audioEngine.initialize();
    if (!audioSuccess)
    {
        return false;
    }

    return true;
}

void Application::runMainLoop(const std::string& targetIp)
{
    m_isRunning = true;
    m_audioEngine.startStream();

    std::cout << "Starting main loop. Sending audio to: " << targetIp << std::endl;

    while (m_isRunning)
    {
        // 1. Process outgoing audio (Mic -> Network)
        std::vector<uint8_t> outgoingPacket = m_audioEngine.getOutgoingPacket();
        if (!outgoingPacket.empty())
        {
            m_networkManager.sendAudioPacket(targetIp, outgoingPacket);
        }

        // 2. Process incoming audio (Network -> Speakers)
        std::vector<uint8_t> incomingPacket = m_networkManager.receiveAudioPacket();
        if (!incomingPacket.empty())
        {
            m_audioEngine.pushIncomingPacket(incomingPacket);
        }

        // 3. Prevent CPU maxing
        // Sleeping for 1 millisecond yields the thread back to the OS, preventing 
        // the while loop from consuming 100% of a CPU core, without adding noticeable latency.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    m_audioEngine.stopStream();
}