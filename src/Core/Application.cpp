// Application.cpp
#include "Core/Application.hpp"
#include "Network/INetworkProvider.hpp"
#include "Utils/WavUtils.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

static constexpr int serverHeartbeatTimeoutMs = 45000;
static constexpr int heartbeatIntervalMs = serverHeartbeatTimeoutMs - 3750;

Application::Application()
{
    m_isRunning = false;
    m_networkProvider = std::make_unique<TailscaleNetwork>();
    spdlog::trace("Application instantiated");
}

Application::~Application()
{
    spdlog::trace("Application shutting down");
    m_isRunning.store(false, std::memory_order_release);
    m_audioEngine.stopStream(); // Stop hardware first
}

bool Application::initialize(bool isServerMode)
{
    m_isServerMode = isServerMode;
    spdlog::debug("Initializing Application in {} mode", m_isServerMode ? "Server" : "Client");

    bool networkSuccess = m_networkProvider->initialize(m_isServerMode);
    if (!networkSuccess)
    {
        spdlog::critical("Network provider failed to initialize");
        return false;
    }

    m_networkManager.setProvider(m_networkProvider.get());

    if (m_isServerMode)
    {
        m_dbManager = std::make_unique<DatabaseManager>();
        if (!m_dbManager->initialize("chat_history.db"))
        {
            spdlog::error("Failed to initialize sqlite db.");
        }
        else
        {
            spdlog::info("Database initialized successfully");
        }
        return true;
    }

    bool audioSuccess = m_audioEngine.initialize();
    if (!audioSuccess)
    {
        spdlog::critical("Audio engine failed to initialize");
        return false;
    }

    if (!m_windowManager.initialize())
    {
        spdlog::error("Failed to initialize WindowManager");
        return false;
    }

    spdlog::info("Client Application initialized successfully");
    return true;
}

void Application::runMainLoop(const std::string& targetIp)
{
    m_isRunning.store(true, std::memory_order_release);
    
    if (m_isServerMode)
    {
        spdlog::info("Starting Server Engine...");
        m_networkProvider->setUdpReceiveCallback([this](const auto& endpoint, const auto* data, auto size)
        {
            onServerUdpPacket(endpoint, data, size);
        });
        
        m_controlThread = std::thread(&Application::serverControlLoop, this);
        
        while (m_isRunning.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    else
    {
spdlog::info("Starting Client Engine targeting: {}", targetIp);
        m_networkProvider->setUdpReceiveCallback([this](const auto& endpoint, const auto* data, auto size)
        {
            onClientUdpPacket(endpoint, data, size);
        });
        
        //Spawn background threads
        m_controlThread = std::thread(&Application::clientControlLoop, this, targetIp);
        m_routerThread = std::thread(&Application::clientOutgoingAudioLoop, this, targetIp);

        //Hand main thread to Qt Native Event Loop
        // Execution pauses here until the window is closed.
        m_windowManager.exec();

        //User closed the window, exec() returned. Initiate shutdown.
        spdlog::info("Client window closed, initiating shutdown sequence...");
        m_isRunning.store(false, std::memory_order_release);
        
        //Force-wake any sleeping condition variables so threads can exit
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clientCv.notify_all(); 
        }
        
        m_audioEngine.stopStream();
        m_windowManager.cleanup();
    }

    spdlog::debug("Joining background threads...");
    if (m_controlThread.joinable()) m_controlThread.join();
    if (m_routerThread.joinable()) m_routerThread.join();
    spdlog::debug("All threads joined gracefully.");
}

void Application::onServerUdpPacket(const asio::ip::udp::endpoint& senderEndpoint, const uint8_t* payloadData, size_t payloadSize)
{
    PeerRoutingState peerRoutingUpdate;
    while (m_routingQueue.pop(peerRoutingUpdate))
    {
        std::array<uint8_t, uuidLen> uuidKey;
        std::memcpy(uuidKey.data(), peerRoutingUpdate.uuid, uuidLen);
        
        if (m_activeRouters.find(uuidKey) == m_activeRouters.end())
        {
            m_activeRouters[uuidKey] = {asio::ip::udp::endpoint(), false, peerRoutingUpdate.activeChannelId};
        }
        else
        {
            m_activeRouters[uuidKey].activeChannelId = peerRoutingUpdate.activeChannelId;
        }
    }

    if (payloadSize <= uuidLen)
    {
        return;
    }

    std::array<uint8_t, uuidLen> packetSenderUuid;
    std::memcpy(packetSenderUuid.data(), payloadData, uuidLen);

    auto senderIterator = m_activeRouters.find(packetSenderUuid);
    if (senderIterator == m_activeRouters.end())
    {
        return;
    }

    int senderChannel = senderIterator->second.activeChannelId;
    if (senderChannel == -1)
    {
        return;
    }

    senderIterator->second.endpoint = senderEndpoint;
    senderIterator->second.hasValidEndpoint = true;

    auto sharedPayload = std::make_shared<std::vector<uint8_t>>(payloadData, payloadData + payloadSize);

    for (const auto& [otherUuid, profile] : m_activeRouters)
    {
        if (otherUuid != packetSenderUuid && profile.activeChannelId == senderChannel && profile.hasValidEndpoint)
        {
            m_networkProvider->sendDataAsync(profile.endpoint, sharedPayload);
        }
    }
}

void Application::onClientUdpPacket(const asio::ip::udp::endpoint& senderEndpoint, const uint8_t* payloadData, size_t payloadSize)
{
    int currentChannel = m_activeVoiceChannelId.load(std::memory_order_acquire);
    bool isDeafened = m_isDeafened.load(std::memory_order_acquire);

    if (currentChannel != -1 && !isDeafened && payloadSize > uuidLen)
    {
        std::string senderUuid(reinterpret_cast<const char*>(payloadData), uuidLen);
        m_windowManager.markSpeakerActive(senderUuid);
        
        std::vector<uint8_t> opusAudioData(payloadData + uuidLen, payloadData + payloadSize);
        m_audioEngine.pushIncomingPacket(senderUuid, opusAudioData);
    }
}

void Application::serverControlLoop()
{
    spdlog::trace("Server control loop started");

    struct ClientProfile
    {
        std::string username;
        int activeChannelId;
        bool isMuted;
        bool isDeafened;
        std::string latestUdpEndpoint;
    };

    std::map<std::string, ClientProfile> clientMap;
    
    auto broadcastGlobalVoiceState = [this, &clientMap]()
    {
        spdlog::trace("Broadcasting global voice state to all peers");
        std::string peersPayload = "PUSH_PEERS|";
        for (const auto& [clientUuid, profile] : clientMap)
        {
            if (profile.activeChannelId != -1)
            {
                peersPayload += profile.username + ":" + (profile.isMuted ? "1" : "0") + ":" + (profile.isDeafened ? "1" : "0") + ":" + clientUuid + ":" + std::to_string(profile.activeChannelId) + ",";
            }
        }
        m_networkManager.broadcastTcp(peersPayload);
    };

    auto tcpHandler = [this, &clientMap, &broadcastGlobalVoiceState](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        TcpPayload parsedRequest = parseTcpPayload(payload);
        spdlog::trace("TCP Handler received message of type: {} from UUID: {}", parsedRequest.messageType, parsedRequest.senderUuid);

        std::istringstream dataStream(parsedRequest.rawData);

        if (parsedRequest.messageType == "HEARTBEAT")
        {
            return "HEARTBEAT_ACK";
        }
        else if (parsedRequest.messageType == "LOGIN")
        {
            std::string username;
            std::string clientUdpPort;
            std::getline(dataStream, username, '|');
            std::getline(dataStream, clientUdpPort);
            
            ClientProfile profile;
            profile.username = username;
            profile.activeChannelId = -1;
            profile.isMuted = false;
            profile.isDeafened = false;
            
            std::string rawIp = incomingIp.substr(0, incomingIp.find(':'));
            profile.latestUdpEndpoint = rawIp + ":" + (clientUdpPort.empty() ? "50000" : clientUdpPort);
            
            clientMap[parsedRequest.senderUuid] = profile;
            spdlog::info("User logged in: {} with UUID: {}", username, parsedRequest.senderUuid);
            
            PeerRoutingState routingUpdate;
            std::strncpy(routingUpdate.uuid, parsedRequest.senderUuid.c_str(), uuidLen);
            std::strncpy(routingUpdate.endpoint, profile.latestUdpEndpoint.c_str(), 64);
            routingUpdate.activeChannelId = profile.activeChannelId;
            m_routingQueue.forcePush(routingUpdate);

            broadcastGlobalVoiceState();
            return "ACK";
        }
        else if (parsedRequest.messageType == "STATE")
        {
            std::string channelIdString, mutedString, deafenedString;
            std::getline(dataStream, channelIdString, '|');
            std::getline(dataStream, mutedString, '|');
            std::getline(dataStream, deafenedString, '|');
            
            if (clientMap.find(parsedRequest.senderUuid) != clientMap.end())
            {
                clientMap[parsedRequest.senderUuid].activeChannelId = std::stoi(channelIdString);
                clientMap[parsedRequest.senderUuid].isMuted = (mutedString == "1");
                clientMap[parsedRequest.senderUuid].isDeafened = (deafenedString == "1");

                spdlog::debug("State updated for {}: Channel={}, Muted={}, Deafened={}", 
                            parsedRequest.senderUuid, clientMap[parsedRequest.senderUuid].activeChannelId, clientMap[parsedRequest.senderUuid].isMuted, clientMap[parsedRequest.senderUuid].isDeafened);

                PeerRoutingState routingUpdate;
                std::strncpy(routingUpdate.uuid, parsedRequest.senderUuid.c_str(), uuidLen);
                std::strncpy(routingUpdate.endpoint, clientMap[parsedRequest.senderUuid].latestUdpEndpoint.c_str(), 64);
                routingUpdate.activeChannelId = clientMap[parsedRequest.senderUuid].activeChannelId;
                m_routingQueue.forcePush(routingUpdate);
                
                broadcastGlobalVoiceState();
            }
            return "ACK";
        }
        else if (parsedRequest.messageType == "CHAT")
        {
            std::string channelIdString, message;
            std::getline(dataStream, channelIdString, '|');
            std::getline(dataStream, message);
            
            int channelId = std::stoi(channelIdString);
            std::string username = clientMap.count(parsedRequest.senderUuid) ? clientMap[parsedRequest.senderUuid].username : "Unknown";
            
            spdlog::debug("Chat message received from {} for channel {}", username, channelId);

            if (m_dbManager)
            {
                m_dbManager->storeMessage(channelId, parsedRequest.senderUuid, username, message);
            }

            m_networkManager.broadcastTcp("PUSH_CHAT|" + username + ": " + message);
            return "ACK";
        }
        else if (parsedRequest.messageType == "REQ_CHAT_LOG")
        {
            std::string channelIdString;
            std::getline(dataStream, channelIdString, '|');
            int channelId = std::stoi(channelIdString);
            std::string chatResponse = "CHAT_LOG|";
            
            spdlog::debug("Chat log requested for channel {} by UUID: {}", channelId, parsedRequest.senderUuid);

            if (m_dbManager)
            {
                auto history = m_dbManager->fetchLastMessages(channelId, 50);
                for (const auto& chatMessage : history)
                {
                    chatResponse += chatMessage.username + ": " + chatMessage.message + "\n";
                }
            }
            m_networkManager.sendTcpTo(parsedRequest.senderUuid, chatResponse); 
            return ""; 
        }
        else if (parsedRequest.messageType == "SYNC_CHANNELS" || parsedRequest.messageType == "CREATE_CHANNEL")
        {
            if (parsedRequest.messageType == "CREATE_CHANNEL")
            {
                std::string channelType, channelName;
                std::getline(dataStream, channelType, '|');
                std::getline(dataStream, channelName);
                
                spdlog::info("Creating new channel: [{}] {}", channelType, channelName);
                
                if (m_dbManager)
                {
                    if (channelType == "TEXT") m_dbManager->addTextChannel(channelName);
                    else if (channelType == "VOICE") m_dbManager->addVoiceChannel(channelName);
                }
            }

            std::string channelsResponse = "CHANNELS|";
            if (m_dbManager)
            {
                auto textChannels = m_dbManager->fetchTextChannels();
                channelsResponse += "TEXT:";
                for (const auto& channel : textChannels) channelsResponse += std::to_string(channel.id) + "=" + channel.name + ",";
                channelsResponse += "|VOICE:";
                auto voiceChannels = m_dbManager->fetchVoiceChannels();
                for (const auto& channel : voiceChannels) channelsResponse += std::to_string(channel.id) + "=" + channel.name + ",";
            }
            
            if (parsedRequest.messageType == "SYNC_CHANNELS")
            {
                spdlog::debug("Syncing channels directly to UUID: {}", parsedRequest.senderUuid);
                m_networkManager.sendTcpTo(parsedRequest.senderUuid, channelsResponse);
            }
            else
            {
                spdlog::debug("Broadcasting updated channel list to all clients");
                m_networkManager.broadcastTcp(channelsResponse);
            }
            return ""; 
        }
        
        spdlog::warn("Unknown message type received: {}", parsedRequest.messageType);
        return "UNKNOWN";
    };

    m_networkManager.pollTcpConnections(tcpHandler);

    while (m_isRunning.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Application::clientControlLoop(const std::string& serverIp)
{
    spdlog::trace("Client control loop started");
    std::string localUuid = Utils::getHardwareUUID();
    bool hasLoggedIn = false;
    
    auto lastHeartbeatTime = std::chrono::steady_clock::now();

    auto pushHandler = [this](const std::string& payload)
    {
        processClientTcpPush(payload);
    };

    m_windowManager.setUiUpdateCallback([this]()
    {
        m_clientCv.notify_one(); // Wake loop instantly on UI interaction
    });

    while (m_isRunning.load(std::memory_order_acquire))
    {
        if (!m_windowManager.isLoggedIn())
        {
            std::unique_lock<std::mutex> lock(m_clientMutex);
            m_clientCv.wait_for(lock, std::chrono::milliseconds(250)); 
            continue;
        }

        processActiveClientState(serverIp, localUuid, hasLoggedIn, lastHeartbeatTime, pushHandler);
        
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceHeartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastHeartbeatTime).count();
        int timeUntilHeartbeat = std::max<int>(10, heartbeatIntervalMs - timeSinceHeartbeat);
        
        // Sleep until UI notifies us, or time to send a heartbeat (cap at 2000ms failsafe)
        int sleepTime = std::min<int>(timeUntilHeartbeat, 2000);

        std::unique_lock<std::mutex> lock(m_clientMutex);
        m_clientCv.wait_for(lock, std::chrono::milliseconds(sleepTime));
    }
}

void Application::processActiveClientState(const std::string& serverIp, const std::string& localUuid, bool& hasLoggedIn, std::chrono::steady_clock::time_point& lastHeartbeatTime, const std::function<void(const std::string&)>& pushHandler)
{
    if (!hasLoggedIn)
    {
        spdlog::debug("Attempting to establish persistent TCP connection to {}", serverIp);
        bool connected = m_networkManager.connectPersistentTcp(serverIp, pushHandler);
        if (!connected)
        {
            spdlog::warn("Failed to connect to server. Retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return;
        }

        std::string localUsername = m_windowManager.getUsername();
        int localAudioPort = m_networkManager.getLocalUdpPort();
        
        spdlog::info("Connected. Sending LOGIN packet as {}", localUsername);
        m_networkManager.sendPersistentTcp("LOGIN|" + localUuid + "|" + localUsername + "|" + std::to_string(localAudioPort));
        m_networkManager.sendPersistentTcp("SYNC_CHANNELS|" + localUuid);
        
        int initialTextChannelId = m_windowManager.getSelectedTextChannelId();
        m_textChannelState.joinChannel(initialTextChannelId);
        m_networkManager.sendPersistentTcp("REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(initialTextChannelId));
        
        hasLoggedIn = true;
        lastHeartbeatTime = std::chrono::steady_clock::now();
    }
    
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastHeartbeatTime).count();
    
    if (elapsedTime >= heartbeatIntervalMs)
    {
        spdlog::trace("Sending heartbeat to server");
        m_networkManager.sendPersistentTcp("HEARTBEAT|" + localUuid);
        lastHeartbeatTime = currentTime;
    }
    
    int uiVoiceChannelId = m_windowManager.getActiveVoiceChannelId();
    bool uiMuted = m_windowManager.isMuted();
    bool uiDeafened = m_windowManager.isDeafened();
    int currentVoice = m_activeVoiceChannelId.load(std::memory_order_relaxed);

    if (uiVoiceChannelId != currentVoice || uiMuted != m_isMuted.load(std::memory_order_relaxed) || uiDeafened != m_isDeafened.load(std::memory_order_relaxed))
    {
        spdlog::info("Local voice state changed: Channel={}, Muted={}, Deafened={}", uiVoiceChannelId, uiMuted, uiDeafened);
        
        if (currentVoice == -1 && uiVoiceChannelId != -1) 
        { 
            m_audioEngine.resetBuffers(); 
            m_audioEngine.startStream(); 
        }
        else if (currentVoice != -1 && uiVoiceChannelId == -1) 
        { 
            m_audioEngine.stopStream(); 
            m_audioEngine.resetBuffers(); 
        }

        m_activeVoiceChannelId.store(uiVoiceChannelId, std::memory_order_release);
        m_isMuted.store(uiMuted, std::memory_order_release);
        m_isDeafened.store(uiDeafened, std::memory_order_release);

        m_networkManager.sendPersistentTcp("STATE|" + localUuid + "|" + std::to_string(uiVoiceChannelId) + "|" + (uiMuted ? "1" : "0") + "|" + (uiDeafened ? "1" : "0"));
    }
    
    int uiTextChannelId = m_windowManager.getSelectedTextChannelId();
    
    if (uiTextChannelId != m_textChannelState.getCurrentChannelId())
    {
        spdlog::debug("Joining text channel {}", uiTextChannelId);
        m_textChannelState.joinChannel(uiTextChannelId);
        m_networkManager.sendPersistentTcp("REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(uiTextChannelId));
    }

    std::string outgoingMessage = m_windowManager.getPendingOutgoingMessage();
    if (!outgoingMessage.empty())
    {
        spdlog::debug("Sending chat message to channel {}", uiTextChannelId);
        m_networkManager.sendPersistentTcp("CHAT|" + localUuid + "|" + std::to_string(uiTextChannelId) + "|" + outgoingMessage);
    }

    std::string newTextChannel = m_windowManager.getPendingNewTextChannel();
    if (!newTextChannel.empty())
    {
        spdlog::info("Requesting new text channel: {}", newTextChannel);
        m_networkManager.sendPersistentTcp("CREATE_CHANNEL|" + localUuid + "|TEXT|" + newTextChannel);
    }

    std::string newVoiceChannel = m_windowManager.getPendingNewVoiceChannel();
    if (!newVoiceChannel.empty())
    {
        spdlog::info("Requesting new voice channel: {}", newVoiceChannel);
        m_networkManager.sendPersistentTcp("CREATE_CHANNEL|" + localUuid + "|VOICE|" + newVoiceChannel);
    }
}

void Application::clientOutgoingAudioLoop(const std::string& serverIp)
{
    spdlog::trace("Client outgoing audio loop started");
    std::string localUuid = Utils::getHardwareUUID();

    while (m_isRunning.load(std::memory_order_acquire))
    {
        // Thread sleeps at 0% CPU until hardware provides an audio frame
        std::vector<uint8_t> outgoingAudio = m_audioEngine.waitForOutgoingPacket(250);
        
        if (outgoingAudio.empty())
        {
            continue; // Loop back to check m_isRunning
        }

        int currentChannel = m_activeVoiceChannelId.load(std::memory_order_acquire);
        bool isDeafened = m_isDeafened.load(std::memory_order_acquire);
        bool isMuted = m_isMuted.load(std::memory_order_acquire);

        while (!outgoingAudio.empty())
        {
            if (currentChannel != -1 && !isMuted && !isDeafened)
            {
                auto sfuPacket = std::make_shared<std::vector<uint8_t>>(localUuid.begin(), localUuid.end());
                sfuPacket->insert(sfuPacket->end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkProvider->sendDataAsync(serverIp, sfuPacket);
            }

            // Immediately clear any backlog
            outgoingAudio = m_audioEngine.getOutgoingPacket();
        }
    }
}

void Application::processClientTcpPush(const std::string& payload)
{
    spdlog::trace("Processing client TCP push payload. Length: {}", payload.length());

    if (payload == "ACK" || payload == "HEARTBEAT_ACK")
    {
        spdlog::trace("Server acknowledged request: {}", payload);
        return;
    }
    else if (payload.find("PUSH_CHAT|") == 0)
    {
        std::string newMessage = payload.substr(10);
        m_windowManager.appendChatMessage(newMessage);
    }
    else if (payload.find("CHAT_LOG|") == 0)
    {
        std::vector<std::string> chatHistory;
        std::istringstream chatStream(payload.substr(9));
        std::string chatLine;
        while (std::getline(chatStream, chatLine, '\n'))
        {
            if (!chatLine.empty())
            {
                chatHistory.push_back(chatLine);
            }
        }
        spdlog::debug("Received chat log with {} lines", chatHistory.size());
        m_windowManager.setChatHistory(chatHistory);
    }
    else if (payload.find("PUSH_PEERS|") == 0)
    {
        std::vector<std::string> peersList;
        std::istringstream peersStream(payload.substr(11));
        std::string peerEntry;
        while (std::getline(peersStream, peerEntry, ','))
        {
            if (!peerEntry.empty())
            {
                peersList.push_back(peerEntry);
            }
        }
        m_windowManager.setVoicePeers(peersList);
    }
    else if (payload.find("CHANNELS|") == 0)
    {
        spdlog::debug("Received channel sync data");
        std::string channelsResponse = payload.substr(9);
        size_t pipePosition = channelsResponse.find('|');
        if (pipePosition != std::string::npos)
        {
            std::string textPart = channelsResponse.substr(0, pipePosition);
            std::string voicePart = channelsResponse.substr(pipePosition + 1);
            
            auto parseChannels = [](const std::string& rawString, const std::string& prefix)
            {
                std::vector<std::pair<int, std::string>> parsedChannels;
                if (rawString.find(prefix) == 0)
                {
                    std::istringstream tokenStream(rawString.substr(prefix.length()));
                    std::string token;
                    while (std::getline(tokenStream, token, ','))
                    {
                        if (token.empty())
                        {
                            continue;
                        }
                        size_t equalsPosition = token.find('=');
                        if (equalsPosition != std::string::npos)
                        {
                            parsedChannels.push_back(std::make_pair(std::stoi(token.substr(0, equalsPosition)), token.substr(equalsPosition + 1)));
                        }
                    }
                }
                return parsedChannels;
            };
            m_windowManager.setChannels(parseChannels(textPart, "TEXT:"), parseChannels(voicePart, "VOICE:"));
        }
    }
    else
    {
        spdlog::warn("Received unrecognized push payload prefix: {}", payload);
    }
}

void Application::runAudioQualityTest(const std::string& inputWavPath, const std::string& outputWavPath)
{
    spdlog::info("Starting File-Driven Network Loopback Test...");
    
    m_networkProvider->initialize(false);
    m_audioEngine.initialize();

    std::vector<int16_t> inputPcm = WavUtils::readWav(inputWavPath);
    std::vector<int16_t> outputPcm;
    
    std::string localUuid = Utils::getHardwareUUID();
    int localPort = m_networkProvider->getLocalUdpPort();
    std::string localhostIp = "127.0.0.1:" + std::to_string(localPort);

    m_networkProvider->setUdpReceiveCallback([this, &outputPcm](const asio::ip::udp::endpoint& endpoint, const uint8_t* data, size_t size)
    {
        if (size > uuidLen)
        {
            std::string senderUuid(reinterpret_cast<const char*>(data), uuidLen);
            std::vector<uint8_t> opusData(data + uuidLen, data + size);
            
            std::vector<int16_t> decodedPcm = m_audioEngine.decodePacketDirectly(opusData);
            outputPcm.insert(outputPcm.end(), decodedPcm.begin(), decodedPcm.end());
        }
    });

    const size_t frameSize = 960; 
    
    for (size_t currentSample = 0; currentSample < inputPcm.size(); currentSample += frameSize)
    {
        size_t chunkSize = std::min(frameSize, inputPcm.size() - currentSample);
        std::vector<int16_t> pcmChunk(inputPcm.begin() + currentSample, inputPcm.begin() + currentSample + chunkSize);
        
        if (chunkSize < frameSize)
        {
            pcmChunk.resize(frameSize, 0);
        }

        std::vector<uint8_t> opusPacket = m_audioEngine.encodePacketDirectly(pcmChunk);
        
        auto sfuPacket = std::make_shared<std::vector<uint8_t>>(localUuid.begin(), localUuid.end());
        sfuPacket->insert(sfuPacket->end(), opusPacket.begin(), opusPacket.end());
        
        m_networkProvider->sendData(localhostIp, *sfuPacket);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    WavUtils::writeWav(outputWavPath, outputPcm);
    spdlog::info("Test complete. Output saved to {}", outputWavPath);
}

TcpPayload Application::parseTcpPayload(const std::string& rawPayload)
{
    TcpPayload parsedPayload;
    size_t firstDelimiter = rawPayload.find('|');
    
    if (firstDelimiter == std::string::npos)
    {
        parsedPayload.messageType = rawPayload;
        return parsedPayload;
    }
    
    parsedPayload.messageType = rawPayload.substr(0, firstDelimiter);
    size_t secondDelimiter = rawPayload.find('|', firstDelimiter + 1);
    
    if (secondDelimiter == std::string::npos)
    {
        parsedPayload.senderUuid = rawPayload.substr(firstDelimiter + 1);
        return parsedPayload;
    }
    
    parsedPayload.senderUuid = rawPayload.substr(firstDelimiter + 1, secondDelimiter - firstDelimiter - 1);
    parsedPayload.rawData = rawPayload.substr(secondDelimiter + 1);
    
    return parsedPayload;
}