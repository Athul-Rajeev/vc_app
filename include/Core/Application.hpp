#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <map>
#include <unordered_map>
#include <array>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "Network/INetworkProvider.hpp"
#include "Network/NetworkManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "UI/WindowManager.hpp"
#include "Core/ChannelState.hpp"
#include "Database/DatabaseManager.hpp"
#include "Utils/Utils.hpp"
#include "Network/TailscaleNetwork.hpp"
#include "Utils/LockFreeQueue.hpp"

static constexpr size_t uuidLen = 36;

struct PeerRoutingState
{
    char uuid[uuidLen];
    char endpoint[64];
    int activeChannelId;
};

struct UuidHash
{
    std::size_t operator()(const std::array<uint8_t, uuidLen>& key) const
    {
        std::size_t hashValue = 0;
        for (uint8_t byte : key)
        {
            hashValue ^= std::hash<uint8_t>()(byte) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        }
        return hashValue;
    }
};

struct RouterPeerState
{
    asio::ip::udp::endpoint endpoint;
    bool hasValidEndpoint = false;
    int activeChannelId = -1;
};

struct TcpPayload
{
    std::string messageType;
    std::string senderUuid;
    std::string rawData;
};

class Application
{
public:
    Application();
    ~Application();

    bool initialize(bool isServerMode);
    void runMainLoop(const std::string& targetIp);
    void runAudioQualityTest(const std::string& inputWavPath, const std::string& outputWavPath);

private:
    void processClientTcpPush(const std::string& payload);
    void serverControlLoop();
    void clientControlLoop(const std::string& serverIp);
    void processActiveClientState(const std::string& serverIp, std::chrono::steady_clock::time_point& lastHeartbeatTime, const std::function<void(const std::string&)>& pushHandler);
    void clientOutgoingAudioLoop(const std::string& serverIp);

    void onServerUdpPacket(const asio::ip::udp::endpoint& senderEndpoint, const uint8_t* payloadData, size_t payloadSize);
    void onClientUdpPacket(const asio::ip::udp::endpoint& senderEndpoint, const uint8_t* payloadData, size_t payloadSize);

    TcpPayload parseTcpPayload(const std::string& rawPayload);

    std::thread m_controlThread;
    std::thread m_routerThread;
    std::string m_activeServerUuid;
    std::mutex m_uuidMutex;

    std::mutex m_clientMutex;
    std::condition_variable m_clientCv;

    std::atomic<int> m_activeVoiceChannelId{-1};
    std::atomic<bool> m_isMuted{false};
    std::atomic<bool> m_isDeafened{false};

    LockFreeQueue<PeerRoutingState, 256> m_routingQueue;
    std::unordered_map<std::array<uint8_t, uuidLen>, RouterPeerState, UuidHash> m_activeRouters;

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