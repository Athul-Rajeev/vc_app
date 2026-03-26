#include "Core/Application.hpp"
#include "Core/Utils.hpp"
#include "Network/TailscaleNetwork.hpp"
#include <sstream>
#include <map>
#include <iostream>

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
    };
    std::map<std::string, ClientProfile> clientMap;
    
    auto tcpHandler = [this, &clientMap](const std::string& incomingIp, const std::string& payload) -> std::string
    {
        std::istringstream payloadStream(payload);
        std::string messageType, senderUuid;
        std::getline(payloadStream, messageType, '|');
        std::getline(payloadStream, senderUuid, '|');
        
        if (messageType == "LOGIN") 
        {
            std::string username;
            std::getline(payloadStream, username);
            clientMap[senderUuid] = {username, 0, false, false, incomingIp + ":50000"};
            std::cout << "User logged in: " << username << std::endl;
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
            }
        }
        else if (messageType == "CHAT") 
        {
            std::string channelIdString, message;
            std::getline(payloadStream, channelIdString, '|');
            std::getline(payloadStream, message);
            int channelId = std::stoi(channelIdString);
            std::string username = clientMap.count(senderUuid) ? clientMap[senderUuid].username : "Unknown";
            if (m_dbManager) m_dbManager->storeMessage(channelId, senderUuid, username, message);
            return "ACK";
        }
        else if (messageType == "POLL_CHAT") 
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
        else if (messageType == "NEW_TEXT_CHAN")
        {
            std::string channelName;
            std::getline(payloadStream, channelName);
            if (m_dbManager) m_dbManager->addTextChannel(channelName);
            return "ACK";
        }
        else if (messageType == "NEW_VOICE_CHAN")
        {
            std::string channelName;
            std::getline(payloadStream, channelName);
            if (m_dbManager) m_dbManager->addVoiceChannel(channelName);
            return "ACK";
        }
        
        std::string peersResponse = "PEERS|";
        int requesterChannel = -1;
        if (clientMap.count(senderUuid))
        {
            requesterChannel = clientMap[senderUuid].activeChannelId;
        }
        if (requesterChannel != -1)
        {
            for (const auto& [clientUuid, profile] : clientMap)
            {
                if (profile.activeChannelId == requesterChannel)
                {
                    peersResponse += profile.username + ":" + (profile.isMuted ? "1" : "0") + ":" + (profile.isDeafened ? "1" : "0") + ":" + clientUuid + ",";
                }
            }
        }
        return peersResponse;
    };

    while (m_isRunning)
    {
        m_networkManager.pollTcpConnections(tcpHandler);
        
        NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
        if (!incomingPacket.payload.empty() && !incomingPacket.senderIp.empty()) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (incomingPacket.payload.size() > 36) 
        {
            std::string packetSenderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), 36);
            if (clientMap.count(packetSenderUuid)) 
            {
                clientMap[packetSenderUuid].latestUdpEndpoint = incomingPacket.senderIp;
                int senderChannel = clientMap[packetSenderUuid].activeChannelId;
                for (const auto& [otherUuid, profile] : clientMap) 
                {
                    if (otherUuid != packetSenderUuid && profile.activeChannelId == senderChannel && !profile.latestUdpEndpoint.empty()) 
                    {
                        m_networkManager.sendAudioPacket(profile.latestUdpEndpoint, incomingPacket.payload);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Application::clientThreadLoop(const std::string& serverIp)
{
    std::string localUuid = Utils::getHardwareUUID();
    bool hasLoggedIn = false;
    long syncTimer = 0; 
    int previousTextChannelId = -1;
    
    while (m_isRunning)
    {
        if (m_windowManager.isLoggedIn()) 
        {
            std::string localUsername = m_windowManager.getUsername();
            int currentTextChannelId = m_windowManager.getSelectedTextChannelId();
            
            if (currentTextChannelId != previousTextChannelId) 
            {
                syncTimer = 1000;
                previousTextChannelId = currentTextChannelId;
            }
            
            if (!hasLoggedIn) 
            {
                m_networkManager.sendSynchronousTcp(serverIp, "LOGIN|" + localUuid + "|" + localUsername);
                hasLoggedIn = true;
            }
            
            std::string outgoingMessage = m_windowManager.getPendingOutgoingMessage();
            if (!outgoingMessage.empty()) 
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CHAT|" + localUuid + "|" + std::to_string(currentTextChannelId) + "|" + outgoingMessage);
                syncTimer = 1000; 
            }

            std::string pendingNewTextChannel = m_windowManager.getPendingNewTextChannel();
            if (!pendingNewTextChannel.empty()) m_networkManager.sendSynchronousTcp(serverIp, "NEW_TEXT_CHAN|" + localUuid + "|" + pendingNewTextChannel);
            
            std::string pendingNewVoiceChannel = m_windowManager.getPendingNewVoiceChannel();
            if (!pendingNewVoiceChannel.empty()) m_networkManager.sendSynchronousTcp(serverIp, "NEW_VOICE_CHAN|" + localUuid + "|" + pendingNewVoiceChannel);
            
            if (syncTimer > 500) 
            {
                int activeVoiceChannelId = m_windowManager.getActiveVoiceChannelId();
                std::string statePayload = "STATE|" + localUuid + "|" + std::to_string(activeVoiceChannelId) + "|" + 
                    (m_windowManager.isMuted() ? "1" : "0") + "|" + 
                    (m_windowManager.isDeafened() ? "1" : "0");
                
                std::string peersResponse = m_networkManager.sendSynchronousTcp(serverIp, statePayload);
                if (peersResponse.find("PEERS|") == 0) 
                {
                    std::vector<std::string> peersList;
                    std::istringstream peersStream(peersResponse.substr(6));
                    std::string peerEntry;
                    while (std::getline(peersStream, peerEntry, ','))
                    {
                        if (!peerEntry.empty()) peersList.push_back(peerEntry);
                    }
                    m_windowManager.setVoicePeers(peersList);
                }
                
                std::string chatResponse = m_networkManager.sendSynchronousTcp(serverIp, "POLL_CHAT|" + localUuid + "|" + std::to_string(currentTextChannelId) + "|");
                if (chatResponse.find("CHAT_LOG|") == 0) 
                {
                    std::vector<std::string> chatHistory;
                    std::istringstream chatStream(chatResponse.substr(9));
                    std::string chatLine;
                    while (std::getline(chatStream, chatLine, '\n'))
                    {
                        if (!chatLine.empty()) chatHistory.push_back(chatLine);
                    }
                    m_windowManager.setChatHistory(chatHistory);
                }
                
                std::string channelsResponse = m_networkManager.sendSynchronousTcp(serverIp, "SYNC_CHANNELS|");
                if (channelsResponse.find("CHANNELS|") == 0)
                {
                    channelsResponse = channelsResponse.substr(9);
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
                                    if (token.empty()) continue;
                                    size_t equalsPosition = token.find('=');
                                    if (equalsPosition != std::string::npos)
                                    {
                                        parsedChannels.push_back({std::stoi(token.substr(0, equalsPosition)), token.substr(equalsPosition + 1)});
                                    }
                                }
                            }
                            return parsedChannels;
                        };
                        
                        m_windowManager.setChannels(parseChannels(textPart, "TEXT:"), parseChannels(voicePart, "VOICE:"));
                    }
                }
                
                syncTimer = 0;
            }
            syncTimer++;
            
            std::vector<uint8_t> outgoingAudio = m_audioEngine.getOutgoingPacket();
            if (!outgoingAudio.empty() && !m_windowManager.isMuted() && !m_windowManager.isDeafened()) 
            {
                std::vector<uint8_t> sfuPacket(localUuid.begin(), localUuid.end());
                sfuPacket.insert(sfuPacket.end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkManager.sendAudioPacket(serverIp, sfuPacket);
            }
            
            NetworkPacket incomingPacket = m_networkManager.receiveAudioPacket();
            if (!incomingPacket.payload.empty() && incomingPacket.payload.size() > 36 && !m_windowManager.isDeafened()) 
            {
                std::string senderUuid(reinterpret_cast<char*>(incomingPacket.payload.data()), 36);
                m_windowManager.markSpeakerActive(senderUuid);
                
                std::vector<uint8_t> opusAudioData(incomingPacket.payload.begin() + 36, incomingPacket.payload.end());
                m_audioEngine.pushIncomingPacket(opusAudioData);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}