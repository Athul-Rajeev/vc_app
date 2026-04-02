#include "Core/Application.hpp"
#include <spdlog/spdlog.h>

static constexpr size_t uuidLen = 36;
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
        m_controlThread = std::thread(&Application::serverControlLoop, this);
        m_routerThread = std::thread(&Application::serverRouterLoop, this);
        
        while (m_isRunning.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    else
    {
        spdlog::info("Starting Client Engine targeting: {}", targetIp);
        m_controlThread = std::thread(&Application::clientControlLoop, this, targetIp);
        m_routerThread = std::thread(&Application::clientAudioLoop, this, targetIp);

        while (m_isRunning.load(std::memory_order_acquire) && !m_windowManager.shouldClose())
        {
            m_windowManager.render();
        }

        spdlog::info("Client window closed, initiating shutdown sequence...");
        m_isRunning.store(false, std::memory_order_release);
        m_audioEngine.stopStream();
        m_windowManager.cleanup();
    }

    spdlog::debug("Joining background threads...");
    if (m_controlThread.joinable()) m_controlThread.join();
    if (m_routerThread.joinable()) m_routerThread.join();
    spdlog::debug("All threads joined gracefully.");
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
        std::istringstream payloadStream(payload);
        std::string messageType;
        std::string senderUuid;
        std::getline(payloadStream, messageType, '|');
        std::getline(payloadStream, senderUuid, '|');
        
        spdlog::trace("TCP Handler received message of type: {} from UUID: {}", messageType, senderUuid);

        if (messageType == "HEARTBEAT")
        {
            return "HEARTBEAT_ACK";
        }
        else if (messageType == "LOGIN")
        {
            std::string username;
            std::string clientUdpPort;
            std::getline(payloadStream, username, '|');
            std::getline(payloadStream, clientUdpPort);
            
            ClientProfile profile;
            profile.username = username;
            profile.activeChannelId = -1;
            profile.isMuted = false;
            profile.isDeafened = false;
            
            std::string rawIp = incomingIp.substr(0, incomingIp.find(':'));
            profile.latestUdpEndpoint = rawIp + ":" + (clientUdpPort.empty() ? "50000" : clientUdpPort);
            
            clientMap[senderUuid] = profile;
            spdlog::info("User logged in: {} with UUID: {}", username, senderUuid);
            
            PeerRoutingState routingUpdate;
            std::strncpy(routingUpdate.uuid, senderUuid.c_str(), uuidLen);
            std::strncpy(routingUpdate.endpoint, profile.latestUdpEndpoint.c_str(), 64);
            routingUpdate.activeChannelId = profile.activeChannelId;
            m_routingQueue.forcePush(routingUpdate);

            broadcastGlobalVoiceState();
            return "ACK";
        }
        else if (messageType == "STATE")
        {
            std::string channelIdString, mutedString, deafenedString;
            std::getline(payloadStream, channelIdString, '|');
            std::getline(payloadStream, mutedString, '|');
            std::getline(payloadStream, deafenedString, '|');
            
            if (clientMap.find(senderUuid) != clientMap.end())
            {
                clientMap[senderUuid].activeChannelId = std::stoi(channelIdString);
                clientMap[senderUuid].isMuted = (mutedString == "1");
                clientMap[senderUuid].isDeafened = (deafenedString == "1");

                spdlog::debug("State updated for {}: Channel={}, Muted={}, Deafened={}", 
                              senderUuid, clientMap[senderUuid].activeChannelId, clientMap[senderUuid].isMuted, clientMap[senderUuid].isDeafened);

                PeerRoutingState routingUpdate;
                std::strncpy(routingUpdate.uuid, senderUuid.c_str(), uuidLen);
                std::strncpy(routingUpdate.endpoint, clientMap[senderUuid].latestUdpEndpoint.c_str(), 64);
                routingUpdate.activeChannelId = clientMap[senderUuid].activeChannelId;
                m_routingQueue.forcePush(routingUpdate);
                
                broadcastGlobalVoiceState();
            }
            return "ACK";
        }
        else if (messageType == "CHAT")
        {
            std::string channelIdString, message;
            std::getline(payloadStream, channelIdString, '|');
            std::getline(payloadStream, message);
            
            int channelId = std::stoi(channelIdString);
            std::string username = clientMap.count(senderUuid) ? clientMap[senderUuid].username : "Unknown";
            
            spdlog::debug("Chat message received from {} for channel {}", username, channelId);

            if (m_dbManager)
            {
                m_dbManager->storeMessage(channelId, senderUuid, username, message);
            }

            m_networkManager.broadcastTcp("PUSH_CHAT|" + username + ": " + message);
            return "ACK";
        }
        else if (messageType == "REQ_CHAT_LOG")
        {
            std::string channelIdString;
            std::getline(payloadStream, channelIdString, '|');
            int channelId = std::stoi(channelIdString);
            std::string chatResponse = "CHAT_LOG|";
            
            spdlog::debug("Chat log requested for channel {} by UUID: {}", channelId, senderUuid);

            if (m_dbManager)
            {
                auto history = m_dbManager->fetchLastMessages(channelId, 50);
                for (const auto& chatMessage : history)
                {
                    chatResponse += chatMessage.username + ": " + chatMessage.message + "\n";
                }
            }
            // Push directly back to the requester
            m_networkManager.sendTcpTo(senderUuid, chatResponse); 
            return ""; // Return empty since we manually pushed
        }
        else if (messageType == "SYNC_CHANNELS" || messageType == "CREATE_CHANNEL")
        {
            if (messageType == "CREATE_CHANNEL")
            {
                std::string channelType, channelName;
                std::getline(payloadStream, channelType, '|');
                std::getline(payloadStream, channelName);
                
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
            
            if (messageType == "SYNC_CHANNELS")
            {
                spdlog::debug("Syncing channels directly to UUID: {}", senderUuid);
                m_networkManager.sendTcpTo(senderUuid, channelsResponse);
            }
            else
            {
                spdlog::debug("Broadcasting updated channel list to all clients");
                m_networkManager.broadcastTcp(channelsResponse);
            }
            return ""; 
        }
        
        spdlog::warn("Unknown message type received: {}", messageType);
        return "UNKNOWN";
    };

    m_networkManager.pollTcpConnections(tcpHandler);

    while (m_isRunning.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Application::serverRouterLoop()
{
    spdlog::trace("Server audio router loop started");

    struct RouterPeerState
    {
        std::string endpoint;
        int activeChannelId = -1;
    };
    std::map<std::string, RouterPeerState> activeRouters;

    while (m_isRunning.load(std::memory_order_acquire))
    {
        PeerRoutingState update;
        while (m_routingQueue.pop(update))
        {
            std::string uuidStr(update.uuid, uuidLen);
            activeRouters[uuidStr] = {std::string(update.endpoint), update.activeChannelId};
            spdlog::trace("Router state updated for UUID: {}. Channel: {}", uuidStr, update.activeChannelId);
        }

        bool packetsDrained = false;
        while (true) 
        {
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (incomingPacket.payload.empty())
            {
                break;
            }
            
            packetsDrained = true;
            
            if (incomingPacket.payload.size() <= uuidLen)
            {
                spdlog::trace("Dropped incoming packet: size {} is too small", incomingPacket.payload.size());
                continue;
            }

            std::string packetSenderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);
            if (activeRouters.find(packetSenderUuid) == activeRouters.end())
            {
                continue;
            }

            int senderChannel = activeRouters[packetSenderUuid].activeChannelId;
            if (senderChannel == -1)
            {
                continue;
            }

            activeRouters[packetSenderUuid].endpoint = incomingPacket.senderIp;

            for (const auto& [otherUuid, profile] : activeRouters)
            {
                if (otherUuid != packetSenderUuid && profile.activeChannelId == senderChannel && !profile.endpoint.empty())
                {
                    m_networkManager.sendAudioPacket(profile.endpoint, incomingPacket.payload);
                }
            }
        }
        
        if (!packetsDrained)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
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

    while (m_isRunning.load(std::memory_order_acquire))
    {
        if (m_windowManager.isLoggedIn())
        {
            if (!hasLoggedIn)
            {
                spdlog::debug("Attempting to establish persistent TCP connection to {}", serverIp);
                bool connected = m_networkManager.connectPersistentTcp(serverIp, pushHandler);
                if (!connected)
                {
                    spdlog::warn("Failed to connect to server. Retrying...");
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
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

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Application::clientAudioLoop(const std::string& serverIp)
{
    spdlog::trace("Client audio loop started");
    std::string localUuid = Utils::getHardwareUUID();

    while (m_isRunning.load(std::memory_order_acquire))
    {
        int currentChannel = m_activeVoiceChannelId.load(std::memory_order_acquire);
        bool isDeafened = m_isDeafened.load(std::memory_order_acquire);
        bool isMuted = m_isMuted.load(std::memory_order_acquire);

        bool hasActivity = false;

        while (true)
        {
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (incomingPacket.payload.empty())
            {
                break;
            }
            
            hasActivity = true;

            if (currentChannel != -1 && !isDeafened && incomingPacket.payload.size() > uuidLen)
            {
                std::string senderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);
                m_windowManager.markSpeakerActive(senderUuid);
                
                std::vector<uint8_t> opusAudioData(incomingPacket.payload.begin() + uuidLen, incomingPacket.payload.end());
                m_audioEngine.pushIncomingPacket(senderUuid, opusAudioData);
            }
        }

        while (true)
        {
            std::vector<uint8_t> outgoingAudio = m_audioEngine.getOutgoingPacket();
            if (outgoingAudio.empty())
            {
                break;
            }

            hasActivity = true;

            if (currentChannel != -1 && !isMuted && !isDeafened)
            {
                std::vector<uint8_t> sfuPacket(localUuid.begin(), localUuid.end());
                sfuPacket.insert(sfuPacket.end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkManager.sendAudioPacket(serverIp, sfuPacket);
            }
        }

        if (!hasActivity)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
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