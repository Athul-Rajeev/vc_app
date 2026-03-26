#include "Core/Application.hpp"
#include "Network/TailscaleNetwork.hpp"
Application::Application()
{
    m_isRunning = false;
    m_networkProvider = std::make_unique<TailscaleNetwork>();
}

Application::~Application()
{
    m_isRunning = false;
    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }
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

    if (!m_windowManager.initialize())
    {
        std::cerr << "Failed to initialize WindowManager" << std::endl;
        return false;
    }

    return true;
}

void Application::runMainLoop(const std::string& targetIp)
{
    m_isRunning = true;
    m_audioEngine.startStream();

    std::cout << "Starting application branches. Sending audio to: " << targetIp << std::endl;

    m_networkThread = std::thread(&Application::networkThreadLoop, this, targetIp);

    // Run UI on the main thread to satisfy OS window constraints (like macOS/Windows require)
    while (m_isRunning && !m_windowManager.shouldClose())
    {
        m_windowManager.render();
    }

    m_isRunning = false;
    if (m_networkThread.joinable())
    {
        m_networkThread.join();
    }

    m_audioEngine.stopStream();
    m_windowManager.cleanup();
}

void Application::networkThreadLoop(const std::string& targetIp)
{
    while (m_isRunning)
    {
        // 1. Process outgoing audio (Mic -> Network)
        std::vector<uint8_t> outgoingPacket = m_audioEngine.getOutgoingPacket();
        if (!outgoingPacket.empty())
        {
            if (!m_windowManager.isMuted())
            {
                m_networkManager.sendAudioPacket(targetIp, outgoingPacket);
            }
        }

        // 2. Process incoming audio (Network -> Speakers)
        std::vector<uint8_t> incomingPacket = m_networkManager.receiveAudioPacket();
        if (!incomingPacket.empty())
        {
            if (!m_windowManager.isDeafened())
            {
                m_audioEngine.pushIncomingPacket(incomingPacket);
            }
        }

        // 3. Prevent CPU maxing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}