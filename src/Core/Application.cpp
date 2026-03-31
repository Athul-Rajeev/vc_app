#include "Core/Application.hpp"

static constexpr size_t uuidLen = 36;

Application::Application()
{
    m_isRunning = false;
    m_networkProvider = std::make_unique<TailscaleNetwork>();
}

Application::~Application()
{
    m_isRunning.store(false, std::memory_order_release);
    m_audioEngine.stopStream(); // Stop hardware first

    if (m_controlThread.joinable())
    {
        m_controlThread.join();
    }
    if (m_routerThread.joinable())
    {
        m_routerThread.join();
    }
}

bool Application::initialize(bool isServerMode)
{
    m_isServerMode = isServerMode;

    bool networkSuccess = m_networkProvider->initialize(m_isServerMode);
    if (!networkSuccess)
    {
        return false;
    }

    m_networkManager.setProvider(m_networkProvider.get());

    if (m_isServerMode)
    {
        m_dbManager = std::make_unique<DatabaseManager>();
        if (!m_dbManager->initialize("chat_history.db"))
        {
            std::cerr << "Failed to initialize sqlite db." << std::endl;
        }
        return true;
    }

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
    m_isRunning.store(true, std::memory_order_release);
    
    if (m_isServerMode)
    {
        std::cout << "Starting Server Engine..." << std::endl;
        m_controlThread = std::thread(&Application::serverControlLoop, this);
        m_routerThread = std::thread(&Application::serverRouterLoop, this);
        
        while (m_isRunning.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    else
    {
        std::cout << "Starting Client Engine targeting: " << targetIp << std::endl;
        m_controlThread = std::thread(&Application::clientControlLoop, this, targetIp);
        m_routerThread = std::thread(&Application::clientAudioLoop, this, targetIp);

        while (m_isRunning.load(std::memory_order_acquire) && !m_windowManager.shouldClose())
        {
            m_windowManager.render();
        }

        m_isRunning.store(false, std::memory_order_release);
        m_audioEngine.stopStream();
        m_windowManager.cleanup();
    }

    if (m_controlThread.joinable()) m_controlThread.join();
    if (m_routerThread.joinable()) m_routerThread.join();
}

void Application::serverControlLoop()
{
    struct ClientProfile
    {
        std::string username;
        int activeChannelId;
        bool isMuted;
        bool isDeafened;
        std::string latestUdpEndpoint;
        std::string tcpEndpoint;
    };

    std::map<std::string, ClientProfile> clientMap;
    
    auto broadcastGlobalVoiceState = [this, &clientMap]()
    {
        std::string peersPayload = "PUSH_PEERS|";
        
        for (const auto& [clientUuid, profile] : clientMap)
        {
            if (profile.activeChannelId != -1)
            {
                peersPayload += profile.username + ":" 
                                + (profile.isMuted ? "1" : "0") + ":" 
                                + (profile.isDeafened ? "1" : "0") + ":" 
                                + clientUuid + ":" 
                                + std::to_string(profile.activeChannelId) + ",";
            }
        }
        
        std::thread([this, peersPayload, currentMap = clientMap]()
        {
            for (const auto& [clientUuid, profile] : currentMap)
            {
                if (!profile.tcpEndpoint.empty())
                {
                    m_networkManager.sendSynchronousTcp(profile.tcpEndpoint, peersPayload);
                }
            }
        }).detach();
    };

    auto tcpHandler = [this, &clientMap, &broadcastGlobalVoiceState](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        std::istringstream payloadStream(payload);
        std::string messageType;
        std::string senderUuid;
        std::getline(payloadStream, messageType, '|');
        std::getline(payloadStream, senderUuid, '|');
        
        if (messageType == "LOGIN")
        {
            std::string username;
            std::string clientTcpPort;
            std::string clientUdpPort;
            
            std::getline(payloadStream, username, '|');
            std::getline(payloadStream, clientTcpPort, '|');
            std::getline(payloadStream, clientUdpPort);
            
            ClientProfile profile;
            profile.username = username;
            profile.activeChannelId = -1;
            profile.isMuted = false;
            profile.isDeafened = false;
            
            std::string pushPort = clientTcpPort.empty() ? "50001" : clientTcpPort;
            profile.tcpEndpoint = incomingIp + ":" + pushPort; 
            
            // Bind the OS-assigned UDP port
            std::string audioPort = clientUdpPort.empty() ? "50000" : clientUdpPort;
            profile.latestUdpEndpoint = incomingIp + ":" + audioPort;
            
            clientMap[senderUuid] = profile;
            std::cout << "User logged in: " << username << " [TCP: " << pushPort << ", UDP: " << audioPort << "]" << std::endl;
            
            // Push initial routing state to the UDP thread
            PeerRoutingState routingUpdate;
            std::strncpy(routingUpdate.uuid, senderUuid.c_str(), 36);
            std::strncpy(routingUpdate.endpoint, profile.latestUdpEndpoint.c_str(), 64);
            routingUpdate.activeChannelId = profile.activeChannelId;
            m_routingQueue.forcePush(routingUpdate);

            broadcastGlobalVoiceState();

            return "ACK";
        }
        else if (messageType == "STATE")
        {
            std::string channelIdString;
            std::string mutedString;
            std::string deafenedString;
            std::getline(payloadStream, channelIdString, '|');
            std::getline(payloadStream, mutedString, '|');
            std::getline(payloadStream, deafenedString, '|');
            
            if (clientMap.find(senderUuid) != clientMap.end())
            {
                clientMap[senderUuid].activeChannelId = std::stoi(channelIdString);
                clientMap[senderUuid].isMuted = (mutedString == "1");
                clientMap[senderUuid].isDeafened = (deafenedString == "1");

                // Push updated routing state to the UDP thread
                PeerRoutingState routingUpdate;
                std::strncpy(routingUpdate.uuid, senderUuid.c_str(), 36);
                std::strncpy(routingUpdate.endpoint, clientMap[senderUuid].latestUdpEndpoint.c_str(), 64);
                routingUpdate.activeChannelId = clientMap[senderUuid].activeChannelId;
                m_routingQueue.forcePush(routingUpdate);
                
                broadcastGlobalVoiceState();
            }
            return "ACK";
        }
        else if (messageType == "CHAT")
        {
            std::string channelIdString;
            std::string message;
            std::getline(payloadStream, channelIdString, '|');
            std::getline(payloadStream, message);
            
            int channelId = std::stoi(channelIdString);
            std::string username = clientMap.count(senderUuid) ? clientMap[senderUuid].username : "Unknown";
            
            if (m_dbManager)
            {
                m_dbManager->storeMessage(channelId, senderUuid, username, message);
            }

            // PUSH architecture: Broadcast new message to all clients
            std::string pushMessage = "PUSH_CHAT|" + username + ": " + message;
            
            // Execute the chat push asynchronously
            std::thread([this, pushMessage, currentMap = clientMap]()
            {
                for (const auto& [clientUuid, profile] : currentMap)
                {
                    if (!profile.tcpEndpoint.empty())
                    {
                        m_networkManager.sendSynchronousTcp(profile.tcpEndpoint, pushMessage);
                    }
                }
            }).detach();

            return "ACK";
        }
        else if (messageType == "REQ_CHAT_LOG")
        {
            std::string channelIdString;
            std::getline(payloadStream, channelIdString, '|');
            int channelId = std::stoi(channelIdString);
            std::string chatResponse = "CHAT_LOG|";
            
            if (m_dbManager)
            {
                auto history = m_dbManager->fetchLastMessages(channelId, 50);
                for (const auto& chatMessage : history)
                {
                    chatResponse += chatMessage.username + ": " + chatMessage.message + "\n";
                }
            }
            return chatResponse;
        }
        else if (messageType == "SYNC_CHANNELS")
        {
            std::string response = "CHANNELS|";
            if (m_dbManager)
            {
                auto textChannels = m_dbManager->fetchTextChannels();
                response += "TEXT:";
                for (const auto& channel : textChannels)
                {
                    response += std::to_string(channel.id) + "=" + channel.name + ",";
                }
                
                response += "|VOICE:";
                auto voiceChannels = m_dbManager->fetchVoiceChannels();
                for (const auto& channel : voiceChannels)
                {
                    response += std::to_string(channel.id) + "=" + channel.name + ",";
                }
            }
            return response;
        }
        else if (messageType == "CREATE_CHANNEL")
        {
            std::string channelType;
            std::string channelName;
            std::getline(payloadStream, channelType, '|');
            std::getline(payloadStream, channelName);

            if (m_dbManager)
            {
                if (channelType == "TEXT")
                {
                    m_dbManager->addTextChannel(channelName);
                }
                else if (channelType == "VOICE")
                {
                    m_dbManager->addVoiceChannel(channelName);
                }
            }

            // Rebuild the channel list from the database
            std::string channelsResponse = "CHANNELS|";
            if (m_dbManager)
            {
                auto textChannels = m_dbManager->fetchTextChannels();
                channelsResponse += "TEXT:";
                for (const auto& channel : textChannels)
                {
                    channelsResponse += std::to_string(channel.id) + "=" + channel.name + ",";
                }
                
                channelsResponse += "|VOICE:";
                auto voiceChannels = m_dbManager->fetchVoiceChannels();
                for (const auto& channel : voiceChannels)
                {
                    channelsResponse += std::to_string(channel.id) + "=" + channel.name + ",";
                }
            }

            // Asynchronously push the new channel list to all connected clients
            std::thread([this, channelsResponse, currentMap = clientMap]()
            {
                for (const auto& [clientUuid, profile] : currentMap)
                {
                    if (!profile.tcpEndpoint.empty())
                    {
                        m_networkManager.sendSynchronousTcp(profile.tcpEndpoint, channelsResponse);
                    }
                }
            }).detach();

            return "ACK";
        }
        return "UNKNOWN";
    };

    while (m_isRunning.load(std::memory_order_acquire))
    {
        m_networkManager.waitForEvents(10);
        m_networkManager.pollTcpConnections(tcpHandler);
    }
}

void Application::serverRouterLoop()
{
    struct RouterPeerState
    {
        std::string endpoint;
        int activeChannelId = -1;
    };
    std::map<std::string, RouterPeerState> activeRouters;

    while (m_isRunning.load(std::memory_order_acquire))
    {
        // 1. Drain Lock-Free Routing Updates from Control Thread
        PeerRoutingState update;
        while (m_routingQueue.pop(update))
        {
            std::string uuidStr(update.uuid, 36);
            activeRouters[uuidStr] = {std::string(update.endpoint), update.activeChannelId};
        }

        // 2. Socket Draining Loop: Pull everything out of the OS buffer instantly
        bool packetsDrained = false;
        while (true) 
        {
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (incomingPacket.payload.empty())
            {
                break; // OS Buffer empty
            }
            
            packetsDrained = true;
            
            if (incomingPacket.payload.size() <= uuidLen) continue;

            std::string packetSenderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);
            if (activeRouters.find(packetSenderUuid) == activeRouters.end()) continue;

            int senderChannel = activeRouters[packetSenderUuid].activeChannelId;
            if (senderChannel == -1) continue;

            // Auto-update endpoint if it changed
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
    std::string localUuid = Utils::getHardwareUUID();
    bool hasLoggedIn = false;
    
    auto clientTcpHandler = [this](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        processClientTcpPush(payload);
        return "ACK";
    };

    while (m_isRunning.load(std::memory_order_acquire))
    {
        m_networkManager.waitForEvents(5);
        m_networkManager.pollTcpConnections(clientTcpHandler);

        if (m_windowManager.isLoggedIn())
        {
            if (!hasLoggedIn)
            {
                std::string localUsername = m_windowManager.getUsername();
                int localPushPort = m_networkManager.getLocalTcpPort();
                int localAudioPort = m_networkManager.getLocalUdpPort();
                
                // Append BOTH ports to the LOGIN packet
                m_networkManager.sendSynchronousTcp(serverIp, "LOGIN|" + localUuid + "|" + localUsername + "|" + std::to_string(localPushPort) + "|" + std::to_string(localAudioPort));
                
                std::string channelsResponse = m_networkManager.sendSynchronousTcp(serverIp, "SYNC_CHANNELS|");
                processClientTcpPush(channelsResponse);
                
                // Force initial chat log fetch so the screen isn't blank
                int initialTextChannelId = m_windowManager.getSelectedTextChannelId();
                m_textChannelState.joinChannel(initialTextChannelId);
                std::string logResponse = m_networkManager.sendSynchronousTcp(serverIp, "REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(initialTextChannelId));
                processClientTcpPush(logResponse);
                
                hasLoggedIn = true;
            }
            
            // 2. Event-Driven Changes
            int uiVoiceChannelId = m_windowManager.getActiveVoiceChannelId();
            bool uiMuted = m_windowManager.isMuted();
            bool uiDeafened = m_windowManager.isDeafened();

            int currentVoice = m_activeVoiceChannelId.load(std::memory_order_relaxed);

            if (uiVoiceChannelId != currentVoice || uiMuted != m_isMuted.load(std::memory_order_relaxed) || uiDeafened != m_isDeafened.load(std::memory_order_relaxed))
            {
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

                std::string statePayload = "STATE|" + localUuid + "|" + std::to_string(uiVoiceChannelId) + "|" + (uiMuted ? "1" : "0") + "|" + (uiDeafened ? "1" : "0");
                m_networkManager.sendSynchronousTcp(serverIp, statePayload);
            }
            
            // 3. Process Outgoing Events
            int uiTextChannelId = m_windowManager.getSelectedTextChannelId();
            
            if (uiTextChannelId != m_textChannelState.getCurrentChannelId())
            {
                m_textChannelState.joinChannel(uiTextChannelId);
                std::string logResponse = m_networkManager.sendSynchronousTcp(serverIp, "REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(uiTextChannelId));
                processClientTcpPush(logResponse);
            }

            std::string outgoingMessage = m_windowManager.getPendingOutgoingMessage();
            if (!outgoingMessage.empty())
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CHAT|" + localUuid + "|" + std::to_string(uiTextChannelId) + "|" + outgoingMessage);
            }

            std::string newTextChannel = m_windowManager.getPendingNewTextChannel();
            if (!newTextChannel.empty())
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CREATE_CHANNEL|" + localUuid + "|TEXT|" + newTextChannel);
            }

            std::string newVoiceChannel = m_windowManager.getPendingNewVoiceChannel();
            if (!newVoiceChannel.empty())
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CREATE_CHANNEL|" + localUuid + "|VOICE|" + newVoiceChannel);
            }
        }
    }
}

void Application::clientAudioLoop(const std::string& serverIp)
{
    std::string localUuid = Utils::getHardwareUUID();

    while (m_isRunning.load(std::memory_order_acquire))
    {
        int currentChannel = m_activeVoiceChannelId.load(std::memory_order_acquire);
        bool isDeafened = m_isDeafened.load(std::memory_order_acquire);
        bool isMuted = m_isMuted.load(std::memory_order_acquire);

        // Always drain buffers to prevent OS socket backup
        bool activity = false;

        // Drain Incoming UDP
        while (true)
        {
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (incomingPacket.payload.empty()) break;
            
            activity = true;

            if (currentChannel != -1 && !isDeafened && incomingPacket.payload.size() > uuidLen)
            {
                std::string senderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);
                m_windowManager.markSpeakerActive(senderUuid);
                
                std::vector<uint8_t> opusAudioData(incomingPacket.payload.begin() + uuidLen, incomingPacket.payload.end());
                m_audioEngine.pushIncomingPacket(senderUuid, opusAudioData);
            }
        }

        // Drain Outgoing Audio Engine Packets
        while (true)
        {
            std::vector<uint8_t> outgoingAudio = m_audioEngine.getOutgoingPacket();
            if (outgoingAudio.empty()) break;

            activity = true;

            if (currentChannel != -1 && !isMuted && !isDeafened)
            {
                std::vector<uint8_t> sfuPacket(localUuid.begin(), localUuid.end());
                sfuPacket.insert(sfuPacket.end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkManager.sendAudioPacket(serverIp, sfuPacket);
            }
        }

        if (!activity)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Application::processClientTcpPush(const std::string& payload)
{
    if (payload.find("PUSH_CHAT|") == 0)
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
}