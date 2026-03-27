#include "Core/Application.hpp"

static constexpr size_t uuidLen = 36;

Application::Application()
{
    m_isRunning = false;
    m_networkProvider = std::make_unique<TailscaleNetwork>();
}

Application::~Application()
{
    m_isRunning = false;
    if (m_networkThread.joinable())
    {
        m_networkThread.join();
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
    m_isRunning = true;
    
    if (m_isServerMode)
    {
        std::cout << "Starting Server Engine..." << std::endl;
        m_networkThread = std::thread(&Application::serverThreadLoop, this);
        
        while (m_isRunning)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    else
    {
        m_audioEngine.startStream();
        std::cout << "Starting Client Engine targeting: " << targetIp << std::endl;
        m_networkThread = std::thread(&Application::clientThreadLoop, this, targetIp);

        while (m_isRunning && !m_windowManager.shouldClose())
        {
            m_windowManager.render();
        }

        m_audioEngine.stopStream();
        m_windowManager.cleanup();
    }

    m_isRunning = false;
    if (m_networkThread.joinable())
    {
        m_networkThread.join();
    }
}

void Application::serverThreadLoop()
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
    
    // Helper to broadcast state to everyone in a specific channel
    auto broadcastPeersToChannel = [this, &clientMap](int channelId)
    {
        std::string peersPayload = "PUSH_PEERS|";
        for (const auto& [clientUuid, profile] : clientMap)
        {
            if (profile.activeChannelId == channelId)
            {
                peersPayload += profile.username + ":" + (profile.isMuted ? "1" : "0") + ":" + (profile.isDeafened ? "1" : "0") + ":" + clientUuid + ",";
            }
        }
        
        for (const auto& [clientUuid, profile] : clientMap)
        {
            if (profile.activeChannelId == channelId && !profile.tcpEndpoint.empty())
            {
                m_networkManager.sendSynchronousTcp(profile.tcpEndpoint, peersPayload);
            }
        }
    };

    auto tcpHandler = [this, &clientMap, &broadcastPeersToChannel](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        std::istringstream payloadStream(payload);
        std::string messageType;
        std::string senderUuid;
        std::getline(payloadStream, messageType, '|');
        std::getline(payloadStream, senderUuid, '|');
        
        if (messageType == "LOGIN")
        {
            std::string username;
            std::getline(payloadStream, username);
            
            ClientProfile profile;
            profile.username = username;
            profile.activeChannelId = 0;
            profile.isMuted = false;
            profile.isDeafened = false;
            profile.latestUdpEndpoint = incomingIp + ":50000";
            profile.tcpEndpoint = incomingIp;
            
            clientMap[senderUuid] = profile;
            std::cout << "User logged in: " << username << std::endl;
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
                int oldChannel = clientMap[senderUuid].activeChannelId;
                int newChannel = std::stoi(channelIdString);
                
                clientMap[senderUuid].activeChannelId = newChannel;
                clientMap[senderUuid].isMuted = (mutedString == "1");
                clientMap[senderUuid].isDeafened = (deafenedString == "1");

                // Push new peer states to old channel and new channel
                if (oldChannel != newChannel)
                {
                    broadcastPeersToChannel(oldChannel);
                }
                broadcastPeersToChannel(newChannel);
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
            for (const auto& [clientUuid, profile] : clientMap)
            {
                if (!profile.tcpEndpoint.empty())
                {
                    m_networkManager.sendSynchronousTcp(profile.tcpEndpoint, pushMessage);
                }
            }
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
        return "UNKNOWN";
    };

    while (m_isRunning)
    {
        // Event-driven block: Sleeps thread via OS interrupts, 
        // waking instantly on packet arrival or maxing at 10ms.
        m_networkManager.waitForEvents(10);

        m_networkManager.pollTcpConnections(tcpHandler);
        NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
        
        if (incomingPacket.payload.empty() || incomingPacket.senderIp.empty() || incomingPacket.payload.size() <= uuidLen)
        {
            continue;
        }

        std::string packetSenderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);

        if (clientMap.find(packetSenderUuid) == clientMap.end())
        {
            continue;
        }

        auto& senderProfile = clientMap[packetSenderUuid];
        senderProfile.latestUdpEndpoint = incomingPacket.senderIp;
        int senderChannel = senderProfile.activeChannelId;

        for (const auto& [otherUuid, profile] : clientMap)
        {
            bool isSelf = (otherUuid == packetSenderUuid);
            bool sameChannel = (profile.activeChannelId == senderChannel);
            bool hasEndpoint = !profile.latestUdpEndpoint.empty();

            if (!isSelf && sameChannel && hasEndpoint)
            {
                m_networkManager.sendAudioPacket(profile.latestUdpEndpoint, incomingPacket.payload);
            }
        }
    }
}

void Application::clientThreadLoop(const std::string& serverIp)
{
    std::string localUuid = Utils::getHardwareUUID();
    bool hasLoggedIn = false;
    
    // Client-side push listener
    auto clientTcpHandler = [this](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        processClientTcpPush(payload);
        return "ACK";
    };

    while (m_isRunning)
    {
        //1. Block to save client CPU, freeing up cycles for the UI/Audio threads.
        // It provides quick UI responsiveness (5ms poll rate) without spin-locking.
        m_networkManager.waitForEvents(5);

        m_networkManager.pollTcpConnections(clientTcpHandler);

        if (m_windowManager.isLoggedIn())
        {
            if (!hasLoggedIn)
            {
                std::string localUsername = m_windowManager.getUsername();
                m_networkManager.sendSynchronousTcp(serverIp, "LOGIN|" + localUuid + "|" + localUsername);
                
                std::string channelsResponse = m_networkManager.sendSynchronousTcp(serverIp, "SYNC_CHANNELS|");
                processClientTcpPush(channelsResponse);
                
                // Force initial chat log fetch so the screen isn't blank
                int initialTextChannelId = m_windowManager.getSelectedTextChannelId();
                m_textChannelState.joinChannel(initialTextChannelId);
                std::string logResponse = m_networkManager.sendSynchronousTcp(serverIp, "REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(initialTextChannelId));
                processClientTcpPush(logResponse);
                
                hasLoggedIn = true;
            }
            
            // 2. Event-Driven Changes: Only hit the network if the user changes state
            int uiTextChannelId = m_windowManager.getSelectedTextChannelId();
            if (uiTextChannelId != m_textChannelState.getCurrentChannelId())
            {
                m_textChannelState.joinChannel(uiTextChannelId);
                std::string logResponse = m_networkManager.sendSynchronousTcp(serverIp, "REQ_CHAT_LOG|" + localUuid + "|" + std::to_string(uiTextChannelId));
                processClientTcpPush(logResponse);
            }

            int uiVoiceChannelId = m_windowManager.getActiveVoiceChannelId();
            bool uiMuted = m_windowManager.isMuted();
            bool uiDeafened = m_windowManager.isDeafened();

            // Needs an update flag for mute/deaf state, simplified here to check channel
            if (uiVoiceChannelId != m_voiceChannelState.getCurrentChannelId())
            {
                m_voiceChannelState.joinChannel(uiVoiceChannelId);
                std::string statePayload = "STATE|" + localUuid + "|" + std::to_string(uiVoiceChannelId) + "|" + 
                    (uiMuted ? "1" : "0") + "|" + 
                    (uiDeafened ? "1" : "0");
                m_networkManager.sendSynchronousTcp(serverIp, statePayload);
            }
            
            // 3. Process Outgoing Events
            std::string outgoingMessage = m_windowManager.getPendingOutgoingMessage();
            if (!outgoingMessage.empty())
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CHAT|" + localUuid + "|" + std::to_string(uiTextChannelId) + "|" + outgoingMessage);
            }
            
            // 4. Audio Processing
            std::vector<uint8_t> outgoingAudio = m_audioEngine.getOutgoingPacket();
            if (!outgoingAudio.empty() && !uiMuted && !uiDeafened)
            {
                std::vector<uint8_t> sfuPacket(localUuid.begin(), localUuid.end());
                sfuPacket.insert(sfuPacket.end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkManager.sendAudioPacket(serverIp, sfuPacket);
            }
            
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (!incomingPacket.payload.empty() && incomingPacket.payload.size() > uuidLen && !uiDeafened)
            {
                std::string senderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), uuidLen);
                m_windowManager.markSpeakerActive(senderUuid);
                
                std::vector<uint8_t> opusAudioData(incomingPacket.payload.begin() + uuidLen, incomingPacket.payload.end());
                m_audioEngine.pushIncomingPacket(opusAudioData);
            }
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