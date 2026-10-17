// Application.cpp
#include "Core/Application.hpp"
#include "UI/DiscordBackend.hpp"
#include "Network/INetworkProvider.hpp"
#include "Utils/WavUtils.hpp"
#include "Security/LoginRateLimiter.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <iostream>

static constexpr int serverHeartbeatTimeoutMs = 45000;
static constexpr int heartbeatIntervalMs = serverHeartbeatTimeoutMs - 3750;

// Chat history page size served per request (initial load and scroll-up paging).
static constexpr int kChatHistoryPageSize = 50;

/// Server-side boundary check for user-submitted names. Wire payloads are '|'-delimited
/// and parsed with ':' and "=,"-style separators (see CONTEXT.md §5), so those characters
/// (and control characters) must never reach the protocol.
static constexpr int kMaxUsernameLength = 32;

static bool isValidDisplayName(const std::string& name)
{
    if (name.empty() || static_cast<int>(name.size()) > kMaxUsernameLength)
    {
        return false;
    }

    for (unsigned char character : name)
    {
        if (character == '|' || character == ':' || character == '=' || character == ',' ||
            character == '\n' || character == '\r' || character < 0x20)
        {
            return false;
        }
    }

    return true;
}

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

        spdlog::info("Server console ready. Type 'help' for available commands.");
        std::thread consoleThread([this]() { serverConsoleLoop(); });
        consoleThread.detach();   // Reads stdin; ends naturally with the process.
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

// Admin console: lightweight commands typed by the server operator.
// A "reset-password <username>" token substitutes for an email on this
// trusted LAN — the operator hands the one-time code to the user directly.
static constexpr int kPasswordResetTokenLifetimeSeconds = 600;

void Application::serverConsoleLoop()
{
    std::string consoleLine;
    while (std::getline(std::cin, consoleLine))
    {
        // Trim surrounding whitespace so trailing newlines/carriage returns don't break parsing.
        std::size_t firstNonSpace = consoleLine.find_first_not_of(" \t\r\n");
        if (firstNonSpace == std::string::npos)
        {
            continue;
        }
        std::size_t lastNonSpace = consoleLine.find_last_not_of(" \t\r\n");
        consoleLine = consoleLine.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);

        constexpr const char* kResetCommand = "reset-password ";

        if (consoleLine == "help")
        {
            std::cout << "Commands: reset-password <username>  (prints a one-time reset token, valid "
                      << kPasswordResetTokenLifetimeSeconds / 60 << " minutes)" << std::endl;
        }
        else if (consoleLine.rfind(kResetCommand, 0) == 0)
        {
            std::string username = consoleLine.substr(std::string(kResetCommand).length());

            AccountInfo account;
            if (username.empty() || !m_dbManager || !m_dbManager->getAccountByUsername(username, account))
            {
                std::cout << "Unknown account: '" << username << "'" << std::endl;
                continue;
            }

            std::string resetToken = Utils::generateRandomUUID();
            long long expiresAtEpochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + kPasswordResetTokenLifetimeSeconds;

            if (!m_dbManager->createPasswordReset(account.accountUuid, Utils::hashString(resetToken), expiresAtEpochSeconds))
            {
                std::cout << "Failed to store reset token in database, aborting." << std::endl;
                continue;
            }

            // The raw token exists in exactly one place: this console print.
            // Only its hash goes to the log file and the database.
            std::cout << "[ADMIN] One-time reset token for '" << username << "':" << std::endl;
            std::cout << "    " << resetToken << std::endl;
            std::cout << "    (valid " << kPasswordResetTokenLifetimeSeconds / 60 << " minutes; hand this to the user)" << std::endl;
            spdlog::info("Password reset token issued for user: {}", username);
        }
        else
        {
            std::cout << "Unknown command. Type 'help'." << std::endl;
        }
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

    // Shared completion path for LOGIN / REGISTER / AUTH_SESSION: registers the client
    // profile, seeds the UDP routing endpoint, and broadcasts the new global voice state.
    auto completeAuthentication = [this, &clientMap, &broadcastGlobalVoiceState](
                                    const std::string& accountUuid,
                                    const std::string& username,
                                    const std::string& incomingIp,
                                    const std::string& clientUdpPort)
    {
        ClientProfile profile;
        profile.username = username;
        profile.activeChannelId = -1;
        profile.isMuted = false;
        profile.isDeafened = false;
        std::string rawIp = incomingIp.substr(0, incomingIp.find(':'));
        profile.latestUdpEndpoint = rawIp + ":" + (clientUdpPort.empty() ? "50000" : clientUdpPort);

        clientMap[accountUuid] = profile;
        spdlog::info("User authenticated: {} with UUID: {}", username, accountUuid);

        // Register endpoint immediately
        PeerRoutingState routingUpdate;
        std::strncpy(routingUpdate.uuid, accountUuid.c_str(), uuidLen);
        std::strncpy(routingUpdate.endpoint, profile.latestUdpEndpoint.c_str(), 64);
        routingUpdate.activeChannelId = profile.activeChannelId;
        m_routingQueue.forcePush(routingUpdate);

        broadcastGlobalVoiceState();
    };

    // Issues a fresh session row for an authenticated account and returns the raw token.
    auto createSessionForAccount = [this](const std::string& accountUuid) -> std::string
    {
        std::string sessionToken = Utils::generateRandomUUID();
        std::string tokenHash = Utils::hashString(sessionToken);
        m_dbManager->createSession(Utils::generateRandomUUID(), accountUuid, tokenHash);
        return sessionToken;
    };

    auto tcpHandler = [this, &clientMap, &broadcastGlobalVoiceState, &completeAuthentication, &createSessionForAccount](const std::string& incomingIp, const std::string& payload) -> std::string
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
            std::string username, password, clientUdpPort;
            std::getline(dataStream, username, '|');
            std::getline(dataStream, password, '|');
            std::getline(dataStream, clientUdpPort);

            std::string sourceIp = incomingIp.substr(0, incomingIp.find(':'));
            std::string accountThrottleKey = "account:" + username.substr(0, 64);

            // Brute-force defense: lockouts come first and never touch the database,
            // so throttled callers get a fast, uniform rejection (no user enumeration
            // and no timing side-channel between throttle states).
            if (m_loginRateLimiter.secondsUntilAllowed(sourceIp) > 0 ||
                m_loginRateLimiter.secondsUntilAllowed(accountThrottleKey) > 0)
            {
                spdlog::warn("Login throttled for {} (user '{}')", sourceIp, username);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return "AUTH_FAILED|Too many attempts";
            }

            // LOGIN must never create an account — registration happens only via REGISTER.
            // Unknown username and wrong password are deliberately the same response:
            // distinguishing them would leak which usernames exist.
            AccountInfo account;
            if (!m_dbManager->getAccountByUsername(username, account))
            {
                spdlog::warn("Login failed for unknown account: {} from {}", username, sourceIp);
                m_loginRateLimiter.recordFailure(sourceIp);
                m_loginRateLimiter.recordFailure(accountThrottleKey);
                return "AUTH_FAILED|Invalid credentials";
            }

            std::string hash = Utils::hashPassword(password, account.passwordSalt);
            if (hash != account.passwordHash)
            {
                spdlog::warn("Login failed, invalid password for user: {} from {}", username, sourceIp);
                m_loginRateLimiter.recordFailure(sourceIp);
                m_loginRateLimiter.recordFailure(accountThrottleKey);
                return "AUTH_FAILED|Invalid credentials";
            }

            std::string sessionToken = createSessionForAccount(account.accountUuid);
            completeAuthentication(account.accountUuid, username, incomingIp, clientUdpPort);
            m_loginRateLimiter.recordSuccess(sourceIp);
            m_loginRateLimiter.recordSuccess(accountThrottleKey);

            return "AUTH_SUCCESS|" + account.accountUuid + "|" + sessionToken + "|" + username;
        }
        else if (parsedRequest.messageType == "REGISTER")
        {
            std::string username, password, clientUdpPort;
            std::getline(dataStream, username, '|');
            std::getline(dataStream, password, '|');
            std::getline(dataStream, clientUdpPort);

            std::string sourceIp = incomingIp.substr(0, incomingIp.find(':'));
            std::string accountThrottleKey = "account:" + username.substr(0, 64);

            if (m_loginRateLimiter.secondsUntilAllowed(sourceIp) > 0 ||
                m_loginRateLimiter.secondsUntilAllowed(accountThrottleKey) > 0)
            {
                spdlog::warn("Registration throttled for {} (user '{}')", sourceIp, username);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return "AUTH_FAILED|Too many attempts";
            }

            if (!isValidDisplayName(username))
            {
                m_loginRateLimiter.recordFailure(sourceIp);
                return "AUTH_FAILED|Registration failed";
            }

            AccountInfo existingAccount;
            if (m_dbManager->getAccountByUsername(username, existingAccount))
            {
                spdlog::warn("Registration rejected, username already taken: {} from {}", username, sourceIp);
                // Counting this as a failure makes username enumeration expensive.
                m_loginRateLimiter.recordFailure(sourceIp);
                return "AUTH_FAILED|Username is already taken";
            }

            if (!m_loginRateLimiter.isAllowedToRegister(sourceIp))
            {
                spdlog::warn("Registration budget exhausted for {}", sourceIp);
                return "AUTH_FAILED|Too many attempts";
            }

            std::string salt = Utils::generateSalt();
            std::string hash = Utils::hashPassword(password, salt);
            std::string accountUuid = Utils::generateRandomUUID();
            if (!m_dbManager->createAccount(accountUuid, username, hash, salt))
            {
                return "AUTH_FAILED|Registration failed";
            }

            spdlog::info("Registered new account: {} with UUID: {} from {}", username, accountUuid, sourceIp);
            m_loginRateLimiter.recordRegistration(sourceIp);
            m_loginRateLimiter.recordSuccess(sourceIp);

            std::string sessionToken = createSessionForAccount(accountUuid);
            completeAuthentication(accountUuid, username, incomingIp, clientUdpPort);

            return "AUTH_SUCCESS|" + accountUuid + "|" + sessionToken + "|" + username;
        }
        else if (parsedRequest.messageType == "RESET_PASSWORD")
        {
            std::string username, resetToken, newPassword, clientUdpPort;
            std::getline(dataStream, username, '|');
            std::getline(dataStream, resetToken, '|');
            std::getline(dataStream, newPassword, '|');
            std::getline(dataStream, clientUdpPort);

            std::string sourceIp = incomingIp.substr(0, incomingIp.find(':'));
            std::string accountThrottleKey = "account:" + username.substr(0, 64);

            if (m_loginRateLimiter.secondsUntilAllowed(sourceIp) > 0 ||
                m_loginRateLimiter.secondsUntilAllowed(accountThrottleKey) > 0)
            {
                spdlog::warn("Password reset throttled for {} (user '{}')", sourceIp, username);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return "AUTH_FAILED|Too many attempts";
            }

            if (!isValidDisplayName(username) || newPassword.empty() || newPassword.size() > 128 ||
                newPassword.find('|') != std::string::npos)
            {
                m_loginRateLimiter.recordFailure(sourceIp);
                return "AUTH_FAILED|Invalid or expired reset token";
            }

            // The token row is consumed on first sight (one-time use), whether or
            // not it was still valid, so replayed tokens disappear immediately.
            AccountInfo account;
            std::string resetTokenHash = Utils::hashString(resetToken);
            if (!m_dbManager->consumePasswordReset(resetTokenHash, account) || account.username != username)
            {
                spdlog::warn("Password reset rejected, invalid/expired token from {} (user '{}')", sourceIp, username);
                m_loginRateLimiter.recordFailure(sourceIp);
                return "AUTH_FAILED|Invalid or expired reset token";
            }

            std::string salt = Utils::generateSalt();
            std::string hash = Utils::hashPassword(newPassword, salt);
            if (!m_dbManager->updateAccountPassword(account.accountUuid, hash, salt))
            {
                return "AUTH_FAILED|Registration failed";
            }

            // Every existing session is dead once the password changes.
            m_dbManager->revokeAllSessions(account.accountUuid);

            spdlog::info("Password reset completed for user: {}", username);
            m_loginRateLimiter.recordSuccess(sourceIp);
            m_loginRateLimiter.recordSuccess(accountThrottleKey);

            std::string sessionToken = createSessionForAccount(account.accountUuid);
            completeAuthentication(account.accountUuid, username, incomingIp, clientUdpPort);

            return "AUTH_SUCCESS|" + account.accountUuid + "|" + sessionToken + "|" + username;
        }
        else if (parsedRequest.messageType == "CHANGE_PASSWORD")
        {
            std::string currentPassword, newPassword;
            std::getline(dataStream, currentPassword, '|');
            std::getline(dataStream, newPassword);

            auto clientProfile = clientMap.find(parsedRequest.senderUuid);
            if (clientProfile == clientMap.end())
            {
                return "PASSWORD_CHANGE_FAILED|Not signed in";
            }

            std::string sourceIp = incomingIp.substr(0, incomingIp.find(':'));
            std::string accountThrottleKey = "account:" + clientProfile->second.username.substr(0, 64);

            if (m_loginRateLimiter.secondsUntilAllowed(accountThrottleKey) > 0)
            {
                spdlog::warn("Password change throttled for {}", accountThrottleKey);
                return "PASSWORD_CHANGE_FAILED|Too many attempts";
            }

            if (currentPassword.empty() || newPassword.empty() || newPassword.size() > 128 ||
                newPassword.find('|') != std::string::npos)
            {
                return "PASSWORD_CHANGE_FAILED|Invalid message";
            }

            AccountInfo account;
            if (!m_dbManager->getAccountByUsername(clientProfile->second.username, account))
            {
                return "PASSWORD_CHANGE_FAILED|Invalid message";
            }

            std::string currentHash = Utils::hashPassword(currentPassword, account.passwordSalt);
            if (currentHash != account.passwordHash)
            {
                spdlog::warn("Password change rejected, wrong current password for user: {}", account.username);
                m_loginRateLimiter.recordFailure(accountThrottleKey);
                return "PASSWORD_CHANGE_FAILED|Incorrect password";
            }

            std::string salt = Utils::generateSalt();
            std::string hash = Utils::hashPassword(newPassword, salt);
            if (!m_dbManager->updateAccountPassword(account.accountUuid, hash, salt))
            {
                return "PASSWORD_CHANGE_FAILED|Invalid message";
            }

            // Devices other than this one are signed out by revoking every session,
            // then issuing the caller a brand-new one to keep it signed in.
            m_dbManager->revokeAllSessions(account.accountUuid);
            m_loginRateLimiter.recordSuccess(accountThrottleKey);

            spdlog::info("Password changed for user: {}", account.username);
            return "PASSWORD_CHANGED|" + createSessionForAccount(account.accountUuid);
        }
        else if (parsedRequest.messageType == "AUTH_SESSION")
        {
            std::string sessionToken, clientUdpPort;
            std::getline(dataStream, sessionToken, '|');
            std::getline(dataStream, clientUdpPort);

            AccountInfo account;
            std::string tokenHash = Utils::hashString(sessionToken);
            
            if (!m_dbManager->getSessionAndAccount(tokenHash, account))
            {
                spdlog::warn("Invalid or expired session token presented.");
                return "AUTH_FAILED|Session expired";
            }

            completeAuthentication(account.accountUuid, account.username, incomingIp, clientUdpPort);

            return "AUTH_SUCCESS|" + account.accountUuid + "|" + sessionToken + "|" + account.username;
        }
        else if (parsedRequest.messageType == "LOGOUT")
        {
            std::string sessionToken;
            std::getline(dataStream, sessionToken);
            
            spdlog::info("User logging out, revoking session. UUID: {}", parsedRequest.senderUuid);
            m_dbManager->revokeSession(Utils::hashString(sessionToken));
            clientMap.erase(parsedRequest.senderUuid);
            
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

            long long nowEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            // Channel id rides along so the client can attach (or reject) the
            // message without guessing the receiving channel.
            m_networkManager.broadcastTcp("PUSH_CHAT|" + std::to_string(channelId) + "|" + username + ": " + message + "|" + std::to_string(nowEpochMs));
            return "ACK";
        }
        else if (parsedRequest.messageType == "REQ_CHAT_LOG")
        {
            std::string channelIdString;
            std::getline(dataStream, channelIdString, '|');
            int channelId = std::stoi(channelIdString);

            spdlog::debug("Chat log requested for channel {} by UUID: {}", channelId, parsedRequest.senderUuid);
            std::string chatResponse = "CHAT_LOG|" + std::to_string(channelId) + "|1|";

            if (m_dbManager)
            {
                bool hasMoreHistory = false;
                auto history = m_dbManager->fetchLastMessages(channelId, kChatHistoryPageSize, hasMoreHistory);
                chatResponse = "CHAT_LOG|" + std::to_string(channelId) + "|" + (hasMoreHistory ? "1" : "0") + "|";
                for (const auto& chatMessage : history)
                {
                    chatResponse += chatMessage.username + ": " + chatMessage.message + "|" + std::to_string(chatMessage.timestamp) + "\n";
                }
            }
            m_networkManager.sendTcpTo(parsedRequest.senderUuid, chatResponse);
            return "";
        }
        else if (parsedRequest.messageType == "REQ_CHAT_LOG_BEFORE")
        {
            // Scroll-up paging: read one page of messages older than the client's
            // oldest loaded row. Response carries real hasMore from the DB.
            std::string channelIdString, beforeString;
            std::getline(dataStream, channelIdString, '|');
            std::getline(dataStream, beforeString);
            int channelId = std::stoi(channelIdString);
            long long beforeTimestampMs = std::stoll(beforeString);

            bool hasMoreHistory = false;
            std::string chatResponse = "CHAT_LOG_MORE|" + std::to_string(channelId) + "|0|";

            if (m_dbManager)
            {
                spdlog::debug("Chat history page requested for channel {} before {} by UUID: {}", channelId, beforeTimestampMs, parsedRequest.senderUuid);
                auto history = m_dbManager->fetchMessagesBefore(channelId, beforeTimestampMs, kChatHistoryPageSize, hasMoreHistory);
                chatResponse = "CHAT_LOG_MORE|" + std::to_string(channelId) + "|" + (hasMoreHistory ? "1" : "0") + "|";
                for (const auto& chatMessage : history)
                {
                    chatResponse += chatMessage.username + ": " + chatMessage.message + "|" + std::to_string(chatMessage.timestamp) + "\n";
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
    
    auto lastHeartbeatTime = std::chrono::steady_clock::now();

    auto pushHandler = [this](const std::string& payload)
    {
        processClientTcpPush(payload);
    };

    m_windowManager.setUiUpdateCallback([this]()
    {
        m_clientCv.notify_one(); 
    });

    while (m_isRunning.load(std::memory_order_acquire))
    {
        processActiveClientState(serverIp, lastHeartbeatTime, pushHandler);
        
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceHeartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastHeartbeatTime).count();
        int timeUntilHeartbeat = std::max<int>(10, heartbeatIntervalMs - timeSinceHeartbeat);
        
        int sleepTime = std::min<int>(timeUntilHeartbeat, 2000);

        std::unique_lock<std::mutex> lock(m_clientMutex);
        m_clientCv.wait_for(lock, std::chrono::milliseconds(sleepTime));
    }
}

void Application::processActiveClientState(const std::string& serverIp, std::chrono::steady_clock::time_point& lastHeartbeatTime, const std::function<void(const std::string&)>& pushHandler)
{
    // Idempotent settings sync: cheap reads, applied every wake so a changed
    // slider value (or one loaded at startup) always reaches the audio engine.
    m_audioEngine.setVoiceActivationThreshold(m_windowManager.getVoiceActivationThreshold());

    std::string currentUuid;
    {
        std::lock_guard<std::mutex> lock(m_uuidMutex);
        currentUuid = m_activeServerUuid;
    }

    // 1. NOT LOGGED IN PHASE
    if (currentUuid.empty())
    {
        // An auth request is already on the wire — wait for the server's push response.
        if (m_authRequestInFlight.load())
        {
            m_windowManager.setLoginState(static_cast<int>(LoginState::Authenticating));
            return; 
        }

        std::string savedToken = Utils::getSavedSessionToken();
        std::string pendingUser, pendingPass, pendingResetToken, pendingNewPassword;
        bool hasManualLogin = m_windowManager.getPendingLogin(pendingUser, pendingPass);
        bool hasPendingRegistration = m_windowManager.getPendingRegistration(pendingUser, pendingPass);
        bool hasPendingReset = m_windowManager.getPendingPasswordReset(pendingUser, pendingResetToken, pendingNewPassword);

        // DO NOT connect to TCP unless we actually have credentials or a token to send.
        // With nothing to send, the client is idle and the UI must show the login
        // form — reporting "connecting" here would hide the form behind the spinner.
        if (savedToken.empty() && !hasManualLogin && !hasPendingRegistration && !hasPendingReset)
        {
            m_windowManager.setLoginState(static_cast<int>(LoginState::Disconnected));
            return; 
        }

        // Credentials or token in hand: we are genuinely about to dial the server.
        m_windowManager.setLoginState(static_cast<int>(LoginState::Connecting));

        spdlog::debug("Connecting to persistent TCP to dispatch auth request...");
        if (!m_networkManager.connectPersistentTcp(serverIp, pushHandler))
        {
            spdlog::warn("Failed to connect to server. Retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return;
        }

        int localAudioPort = m_networkManager.getLocalUdpPort();

        if (hasPendingReset)
        {
            spdlog::info("Submitting password reset for user: {}", pendingUser);
            m_networkManager.sendPersistentTcp("RESET_PASSWORD||" + pendingUser + "|" + pendingResetToken + "|" + pendingNewPassword + "|" + std::to_string(localAudioPort));
        }
        else if (hasPendingRegistration)
        {
            spdlog::info("Registering new account for user: {}", pendingUser);
            m_networkManager.sendPersistentTcp("REGISTER||" + pendingUser + "|" + pendingPass + "|" + std::to_string(localAudioPort));
        }
        else if (hasManualLogin)
        {
            spdlog::info("Attempting login for user: {}", pendingUser);
            m_networkManager.sendPersistentTcp("LOGIN||" + pendingUser + "|" + pendingPass + "|" + std::to_string(localAudioPort));
        }
        else
        {
            spdlog::info("Found saved session token. Attempting silent login...");
            m_networkManager.sendPersistentTcp("AUTH_SESSION||" + savedToken + "|" + std::to_string(localAudioPort));
        }
        
        m_authRequestInFlight.store(true);
        return;
    }

    // 2. LOGGED IN PHASE
    m_authRequestInFlight.store(false); // Successfully authenticated
    m_windowManager.setLoginState(static_cast<int>(LoginState::LoggedIn));

    // Check for Logout Request
    if (m_windowManager.getPendingLogout())
    {
        spdlog::info("User requested logout.");
        m_networkManager.sendPersistentTcp("LOGOUT|" + currentUuid + "|" + Utils::getSavedSessionToken());
        Utils::clearSessionToken();
        {
            std::lock_guard<std::mutex> lock(m_uuidMutex);
            m_activeServerUuid = "";
        }
        m_windowManager.confirmLogout();
        return;
    }

    // Check for Change Password Request
    std::string currentPassword, newPassword;
    if (m_windowManager.getPendingPasswordChange(currentPassword, newPassword))
    {
        spdlog::info("Submitting password change request.");
        m_networkManager.sendPersistentTcp("CHANGE_PASSWORD|" + currentUuid + "|" + currentPassword + "|" + newPassword);
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastHeartbeatTime).count();
    
    if (elapsedTime >= heartbeatIntervalMs)
    {
        spdlog::trace("Sending heartbeat to server");
        m_networkManager.sendPersistentTcp("HEARTBEAT|" + currentUuid);
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

        m_networkManager.sendPersistentTcp("STATE|" + currentUuid + "|" + std::to_string(uiVoiceChannelId) + "|" + (uiMuted ? "1" : "0") + "|" + (uiDeafened ? "1" : "0"));
    }
    
    int uiTextChannelId = m_windowManager.getSelectedTextChannelId();
    
    if (uiTextChannelId != m_textChannelState.getCurrentChannelId())
    {
        spdlog::debug("Joining text channel {}", uiTextChannelId);
        m_textChannelState.joinChannel(uiTextChannelId);
        m_networkManager.sendPersistentTcp("REQ_CHAT_LOG|" + currentUuid + "|" + std::to_string(uiTextChannelId));
    }

    std::string outgoingMessage = m_windowManager.getPendingOutgoingMessage();
    if (!outgoingMessage.empty())
    {
        spdlog::debug("Sending chat message to channel {}", uiTextChannelId);
        m_networkManager.sendPersistentTcp("CHAT|" + currentUuid + "|" + std::to_string(uiTextChannelId) + "|" + outgoingMessage);
    }

    int historyRequestChannelId = -1;
    long long historyRequestBeforeMs = 0;
    if (m_windowManager.getPendingHistoryRequest(historyRequestChannelId, historyRequestBeforeMs))
    {
        spdlog::debug("Requesting chat history page for channel {} before {}", historyRequestChannelId, historyRequestBeforeMs);
        m_networkManager.sendPersistentTcp("REQ_CHAT_LOG_BEFORE|" + currentUuid + "|" + std::to_string(historyRequestChannelId) + "|" + std::to_string(historyRequestBeforeMs));
    }

    std::string newTextChannel = m_windowManager.getPendingNewTextChannel();
    if (!newTextChannel.empty())
    {
        m_networkManager.sendPersistentTcp("CREATE_CHANNEL|" + currentUuid + "|TEXT|" + newTextChannel);
    }

    std::string newVoiceChannel = m_windowManager.getPendingNewVoiceChannel();
    if (!newVoiceChannel.empty())
    {
        m_networkManager.sendPersistentTcp("CREATE_CHANNEL|" + currentUuid + "|VOICE|" + newVoiceChannel);
    }
}

void Application::clientOutgoingAudioLoop(const std::string& serverIp)
{
    spdlog::trace("Client outgoing audio loop started");

    while (m_isRunning.load(std::memory_order_acquire))
    {
        std::vector<uint8_t> outgoingAudio = m_audioEngine.waitForOutgoingPacket(250);
        
        if (outgoingAudio.empty())
        {
            continue; 
        }

        std::string currentUuid;
        {
            std::lock_guard<std::mutex> lock(m_uuidMutex);
            currentUuid = m_activeServerUuid;
        }

        if (currentUuid.empty() || currentUuid.length() != uuidLen)
        {
            m_audioEngine.getOutgoingPacket(); // Drain backlog
            continue;
        }

        int currentChannel = m_activeVoiceChannelId.load(std::memory_order_acquire);
        bool isDeafened = m_isDeafened.load(std::memory_order_acquire);
        bool isMuted = m_isMuted.load(std::memory_order_acquire);

        while (!outgoingAudio.empty())
        {
            if (currentChannel != -1 && !isMuted && !isDeafened)
            {
                auto sfuPacket = std::make_shared<std::vector<uint8_t>>(currentUuid.begin(), currentUuid.end());
                sfuPacket->insert(sfuPacket->end(), outgoingAudio.begin(), outgoingAudio.end());
                m_networkProvider->sendDataAsync(serverIp, sfuPacket);
            }

            outgoingAudio = m_audioEngine.getOutgoingPacket();
        }
    }
}

void Application::processClientTcpPush(const std::string& payload)
{
    spdlog::trace("Processing client TCP push payload. Length: {}", payload.length());

    if (payload.find("AUTH_SUCCESS|") == 0)
    {
        std::istringstream stream(payload.substr(13));
        std::string serverUuid, token, username;
        std::getline(stream, serverUuid, '|');
        std::getline(stream, token, '|');
        std::getline(stream, username);
        
        Utils::saveSessionToken(token);
        
        {
            std::lock_guard<std::mutex> lock(m_uuidMutex);
            m_activeServerUuid = serverUuid;
        }

        m_windowManager.confirmLogin(username);
        
        spdlog::info("Authentication successful. UUID assigned: {}", serverUuid);
        
        m_networkManager.sendPersistentTcp("SYNC_CHANNELS|" + serverUuid);
        int initialTextChannelId = m_windowManager.getSelectedTextChannelId();
        m_textChannelState.joinChannel(initialTextChannelId);
        m_networkManager.sendPersistentTcp("REQ_CHAT_LOG|" + serverUuid + "|" + std::to_string(initialTextChannelId));
        return;
    }
    else if (payload.find("AUTH_FAILED|") == 0)
    {
        std::string reason = payload.substr(12);
        spdlog::warn("Authentication failed: {}", reason);
        Utils::clearSessionToken();
        
        m_windowManager.setLoginError(reason); // Rendered through the backend's curated whitelist
        m_windowManager.confirmLogout();
        
        m_authRequestInFlight.store(false);
        return;
    }
    else if (payload.find("PASSWORD_CHANGED|") == 0)
    {
        std::string refreshedToken = payload.substr(17);
        if (refreshedToken.empty())
        {
            m_windowManager.setPasswordNotice("Rejected");
            return;
        }

        // The server revoked every session when the password changed and issued
        // a fresh one, so this device stays signed in with the new token.
        Utils::saveSessionToken(refreshedToken);
        m_windowManager.setPasswordNotice("Success");
        spdlog::info("Password changed successfully; session token refreshed.");
        return;
    }
    else if (payload.find("PASSWORD_CHANGE_FAILED|") == 0)
    {
        std::string reason = payload.substr(22);
        spdlog::warn("Password change rejected: {}", reason);
        m_windowManager.setPasswordNotice(reason); // Rendered through the backend's curated whitelist
        return;
    }

    if (payload == "ACK" || payload == "HEARTBEAT_ACK")
    {
        spdlog::trace("Server acknowledged request: {}", payload);
        return;
    }
    else if (payload.find("PUSH_CHAT|") == 0)
    {
        // Format: "PUSH_CHAT|<channelId>|username: message|epochMs"
        std::istringstream pushStream(payload.substr(10));
        std::string channelIdString;
        std::getline(pushStream, channelIdString, '|');

        std::string rest, msgText;
        long long epochMs = 0;
        std::getline(pushStream, rest);

        size_t pipePosition = rest.rfind('|');
        if (pipePosition != std::string::npos)
        {
            msgText = rest.substr(0, pipePosition);
            try
            {
                epochMs = std::stoll(rest.substr(pipePosition + 1));
            }
            catch (...) { }
        }
        else
        {
            msgText = rest;
        }

        try
        {
            m_windowManager.appendChatMessage(std::stoi(channelIdString), msgText, epochMs);
        }
        catch (const std::exception&)
        {
            // Malformed channel id — better to drop one broadcast than crash.
            spdlog::warn("Dropping PUSH_CHAT with unparseable channel id");
        }
    }
    else if (payload.find("CHAT_LOG|") == 0)
    {
        // Format: "CHAT_LOG|<channelId>|<hasMore(0|1)>|<user: msg|epochMs lines>"
        std::istringstream headerStream(payload.substr(9));
        std::string channelIdString, hasMoreString;
        std::getline(headerStream, channelIdString, '|');
        std::getline(headerStream, hasMoreString, '|');

        std::vector<std::pair<std::string, long long>> chatHistory;
        std::string chatLine;
        while (std::getline(headerStream, chatLine))
        {
            if (chatLine.empty())
                continue;

            long long epochMs = 0;
            std::string messageText = chatLine;
            size_t lastPipe = chatLine.rfind('|');
            if (lastPipe != std::string::npos)
            {
                try
                {
                    epochMs = std::stoll(chatLine.substr(lastPipe + 1));
                }
                catch (...)
                {
                    epochMs = 0; // fallback; backend will use current time
                }
                messageText = chatLine.substr(0, lastPipe);
            }
            chatHistory.emplace_back(messageText, epochMs);
        }

        try
        {
            bool hasMoreHistory = (hasMoreString == "1");
            spdlog::debug("Received chat log for channel {} with {} lines", channelIdString, chatHistory.size());
            m_windowManager.setChatHistory(std::stoi(channelIdString), hasMoreHistory, chatHistory);
        }
        catch (const std::exception&)
        {
            spdlog::warn("CHAT_LOG parse failure — dropping history page");
        }
    }
    else if (payload.find("CHAT_LOG_MORE|") == 0)
    {
        // Older-messages page from scroll-up paging; same line shape as CHAT_LOG.
        std::istringstream headerStream(payload.substr(14));
        std::string channelIdString, hasMoreString;
        std::getline(headerStream, channelIdString, '|');
        std::getline(headerStream, hasMoreString, '|');

        std::vector<std::pair<std::string, long long>> historyPage;
        std::string chatLine;
        while (std::getline(headerStream, chatLine))
        {
            if (chatLine.empty())
                continue;

            long long epochMs = 0;
            std::string messageText = chatLine;
            size_t lastPipe = chatLine.rfind('|');
            if (lastPipe != std::string::npos)
            {
                try
                {
                    epochMs = std::stoll(chatLine.substr(lastPipe + 1));
                }
                catch (...)
                {
                    epochMs = 0;
                }
                messageText = chatLine.substr(0, lastPipe);
            }
            historyPage.emplace_back(messageText, epochMs);
        }

        try
        {
            bool hasMoreHistory = (hasMoreString == "1");
            spdlog::debug("Received {} older messages for channel {}, hasMore={}", historyPage.size(), channelIdString, hasMoreHistory);
            m_windowManager.prependChatHistory(std::stoi(channelIdString), hasMoreHistory, historyPage);
        }
        catch (const std::exception&)
        {
            spdlog::warn("CHAT_LOG_MORE parse failure — dropping history page");
        }
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
    
    // Generate a temporary UUID for the loopback test
    std::string localUuid = Utils::generateRandomUUID();
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