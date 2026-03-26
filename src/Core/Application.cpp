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
        
        while (m_isRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Headless server
        }
    } 
    else 
    {
        m_audioEngine.startStream();
        std::cout << "Starting Client Engine targeting: " << targetIp << std::endl;
        m_networkThread = std::thread(&Application::clientThreadLoop, this, targetIp);

        while (m_isRunning && !m_windowManager.shouldClose()) {
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
    
    auto tcpHandler = [this, &clientMap](const std::string& incomingIp, const std::string& payload) -> std::string {
        std::istringstream iss(payload);
        std::string type, uuid;
        std::getline(iss, type, '|');
        std::getline(iss, uuid, '|');
        
        if (type == "LOGIN") 
        {
            std::string user;
            std::getline(iss, user);
            clientMap[uuid] = {user, 0, false, false, incomingIp + ":50000"}; // Default 50000 
            std::cout << "User logged in: " << user << std::endl;
        }
        else if (type == "STATE") 
        {
            std::string channelStr, mutedStr, deafStr;
            std::getline(iss, channelStr, '|');
            std::getline(iss, mutedStr, '|');
            std::getline(iss, deafStr, '|');
            if (clientMap.find(uuid) != clientMap.end()) {
                clientMap[uuid].activeChannelId = std::stoi(channelStr);
                clientMap[uuid].isMuted = (mutedStr == "1");
                clientMap[uuid].isDeafened = (deafStr == "1");
            }
        }
        else if (type == "CHAT") 
        {
            std::string channelStr, message;
            std::getline(iss, channelStr, '|');
            std::getline(iss, message);
            int cId = std::stoi(channelStr);
            std::string user = clientMap.count(uuid) ? clientMap[uuid].username : "Unknown";
            if (m_dbManager) m_dbManager->storeMessage(cId, uuid, user, message);
            return "ACK";
        }
        else if (type == "POLL_CHAT") 
        {
            std::string channelStr;
            std::getline(iss, channelStr, '|');
            int cId = std::stoi(channelStr);
            std::string chatResp = "CHAT_LOG|";
            if (m_dbManager) {
                auto history = m_dbManager->fetchLastMessages(cId, 50);
                for (const auto& msg : history) {
                    chatResp += msg.username + ": " + msg.message + "\n";
                }
            }
            return chatResp;
        }
        else if (type == "SYNC_CHANNELS") {
            std::string resp = "CHANNELS|";
            if (m_dbManager) {
                auto txt = m_dbManager->fetchTextChannels();
                resp += "TEXT:";
                for (const auto& c : txt) resp += std::to_string(c.id) + "=" + c.name + ",";
                resp += "|VOICE:";
                auto vc = m_dbManager->fetchVoiceChannels();
                for (const auto& c : vc) resp += std::to_string(c.id) + "=" + c.name + ",";
            }
            return resp;
        }
        else if (type == "NEW_TEXT_CHAN") {
            std::string name;
            std::getline(iss, name);
            if (m_dbManager) m_dbManager->addTextChannel(name);
            return "ACK";
        }
        else if (type == "NEW_VOICE_CHAN") {
            std::string name;
            std::getline(iss, name);
            if (m_dbManager) m_dbManager->addVoiceChannel(name);
            return "ACK";
        }
        
        std::string peersResp = "PEERS|";
        int requesterChannel = -1;
        if (clientMap.count(uuid)) {
            requesterChannel = clientMap[uuid].activeChannelId;
        }
        if (requesterChannel != -1) {
            for (const auto& [id, profile] : clientMap) {
                if (profile.activeChannelId == requesterChannel) {
                    peersResp += profile.username + ":" + (profile.isMuted ? "1" : "0") + ":" + (profile.isDeafened ? "1" : "0") + ":" + id + ",";
                }
            }
        }
        return peersResp;
    };

    while (m_isRunning)
    {
        m_networkManager.pollTcpConnections(tcpHandler);
        
        NetworkPacket pack = m_networkManager.receiveAudioPacket();
        if (!pack.payload.empty() && !pack.senderIp.empty()) 
        {
            if (pack.payload.size() > 36) 
            {
                std::string packUuid(reinterpret_cast<char*>(pack.payload.data()), 36);
                if (clientMap.count(packUuid)) 
                {
                    clientMap[packUuid].latestUdpEndpoint = pack.senderIp;
                    int channel = clientMap[packUuid].activeChannelId;
                    for (const auto& [otherUuid, profile] : clientMap) 
                    {
                        if (otherUuid != packUuid && profile.activeChannelId == channel && !profile.latestUdpEndpoint.empty()) 
                        {
                            m_networkManager.sendAudioPacket(profile.latestUdpEndpoint, pack.payload);
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Application::clientThreadLoop(const std::string& serverIp)
{
    std::string myUuid = Utils::getHardwareUUID();
    bool loggedInSent = false;
    long lastSyncTime = 0; 
    int lastTextChannelId = -1;
    
    while (m_isRunning)
    {
        if (m_windowManager.isLoggedIn()) 
        {
            std::string myUser = m_windowManager.getUsername();
            int currentTextChannel = m_windowManager.getSelectedTextChannelId();
            
            if (currentTextChannel != lastTextChannelId) 
            {
                lastSyncTime = 1000;
                lastTextChannelId = currentTextChannel;
            }
            
            if (!loggedInSent) 
            {
                m_networkManager.sendSynchronousTcp(serverIp, "LOGIN|" + myUuid + "|" + myUser);
                loggedInSent = true;
            }
            
            std::string outgoingMsg = m_windowManager.getPendingOutgoingMessage();
            if (!outgoingMsg.empty()) 
            {
                m_networkManager.sendSynchronousTcp(serverIp, "CHAT|" + myUuid + "|" + std::to_string(currentTextChannel) + "|" + outgoingMsg);
                lastSyncTime = 1000; 
            }

            std::string newTextCh = m_windowManager.getPendingNewTextChannel();
            if (!newTextCh.empty()) m_networkManager.sendSynchronousTcp(serverIp, "NEW_TEXT_CHAN|" + myUuid + "|" + newTextCh);
            
            std::string newVoiceCh = m_windowManager.getPendingNewVoiceChannel();
            if (!newVoiceCh.empty()) m_networkManager.sendSynchronousTcp(serverIp, "NEW_VOICE_CHAN|" + myUuid + "|" + newVoiceCh);
            
            if (lastSyncTime > 500) 
            {
                int activeVoiceChannel = m_windowManager.getActiveVoiceChannelId();
                std::string statePayload = "STATE|" + myUuid + "|" + std::to_string(activeVoiceChannel) + "|" + 
                    (m_windowManager.isMuted() ? "1" : "0") + "|" + 
                    (m_windowManager.isDeafened() ? "1" : "0");
                
                std::string peerResp = m_networkManager.sendSynchronousTcp(serverIp, statePayload);
                if (peerResp.find("PEERS|") == 0) 
                {
                    std::vector<std::string> peers;
                    std::istringstream iss(peerResp.substr(6));
                    std::string p;
                    while(std::getline(iss, p, ',')) { if (!p.empty()) peers.push_back(p); }
                    m_windowManager.setVoicePeers(peers);
                }
                
                std::string chatResp = m_networkManager.sendSynchronousTcp(serverIp, "POLL_CHAT|" + myUuid + "|" + std::to_string(currentTextChannel) + "|");
                if (chatResp.find("CHAT_LOG|") == 0) 
                {
                    std::vector<std::string> history;
                    std::istringstream iss(chatResp.substr(9));
                    std::string line;
                    while(std::getline(iss, line, '\n')) { if (!line.empty()) history.push_back(line); }
                    m_windowManager.setChatHistory(history);
                }
                
                std::string chanResp = m_networkManager.sendSynchronousTcp(serverIp, "SYNC_CHANNELS|");
                if (chanResp.find("CHANNELS|") == 0) {
                    chanResp = chanResp.substr(9);
                    size_t pipePos = chanResp.find('|');
                    if (pipePos != std::string::npos) {
                        std::string textPart = chanResp.substr(0, pipePos);
                        std::string voicePart = chanResp.substr(pipePos + 1);
                        
                        auto parseChannels = [](const std::string& str, const std::string& prefix) {
                            std::vector<std::pair<int, std::string>> res;
                            if (str.find(prefix) == 0) {
                                std::istringstream iss(str.substr(prefix.length()));
                                std::string token;
                                while (std::getline(iss, token, ',')) {
                                    if (token.empty()) continue;
                                    size_t eqPos = token.find('=');
                                    if (eqPos != std::string::npos) {
                                        res.push_back({std::stoi(token.substr(0, eqPos)), token.substr(eqPos + 1)});
                                    }
                                }
                            }
                            return res;
                        };
                        
                        m_windowManager.setChannels(parseChannels(textPart, "TEXT:"), parseChannels(voicePart, "VOICE:"));
                    }
                }
                
                lastSyncTime = 0;
            }
            lastSyncTime++;
            
            std::vector<uint8_t> outgoingAudio = m_audioEngine.getOutgoingPacket();
            if (!outgoingAudio.empty() && !m_windowManager.isMuted() && !m_windowManager.isDeafened()) 
            {
                std::vector<uint8_t> sfuPacket(myUuid.begin(), myUuid.end());
                sfuPacket.insert(sfuPacket.end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkManager.sendAudioPacket(serverIp, sfuPacket);
            }
            
            NetworkPacket pack = m_networkManager.receiveAudioPacket();
            if (!pack.payload.empty() && pack.payload.size() > 36 && !m_windowManager.isDeafened()) 
            {
                std::string sip_uuid(reinterpret_cast<char*>(pack.payload.data()), 36);
                m_windowManager.markSpeakerActive(sip_uuid);
                
                std::vector<uint8_t> opusOnly(pack.payload.begin() + 36, pack.payload.end());
                m_audioEngine.pushIncomingPacket(opusOnly);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}