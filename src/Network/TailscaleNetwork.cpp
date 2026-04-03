#include "Network/TailscaleNetwork.hpp"
#include "Network/INetworkProvider.hpp"
#include <spdlog/spdlog.h>

TailscaleNetwork::TailscaleNetwork()
    : m_tcpContext(), 
      m_udpContext(),
      m_udpSocket(m_udpContext), 
      m_tcpAcceptor(m_tcpContext), 
      m_isServerMode(false)
{
    m_port = 50000;
    m_receiveBuffer.resize(1500);
    spdlog::trace("TailscaleNetwork instantiated");
}

TailscaleNetwork::~TailscaleNetwork()
{
    spdlog::trace("TailscaleNetwork shutting down contexts and joining threads");
    
    m_tcpContext.stop();
    m_udpContext.stop();

    if (m_tcpThread.joinable())
    {
        m_tcpThread.join();
    }
    if (m_udpThread.joinable())
    {
        m_udpThread.join();
    }
    
    spdlog::trace("TailscaleNetwork destroyed");
}

bool TailscaleNetwork::initialize(bool isServerMode)
{
    m_isServerMode = isServerMode;
    spdlog::debug("Initializing TailscaleNetwork in {} mode", m_isServerMode ? "Server" : "Client");

    try
    {
        // 1. Initialize UDP (Data Plane)
        unsigned short udpPortToBind = m_isServerMode ? 50000 : 0;
        spdlog::debug("Opening UDP socket and binding to port {}", udpPortToBind);
        
        m_udpSocket.open(asio::ip::udp::v4());
        asio::ip::udp::endpoint localUdpEndpoint(asio::ip::udp::v4(), udpPortToBind);
        m_udpSocket.bind(localUdpEndpoint);

        // 2. Initialize TCP (Control Plane)
        unsigned short tcpPortToBind = m_isServerMode ? 50001 : 0;
        spdlog::debug("Opening TCP acceptor and binding to port {}", tcpPortToBind);
        
        asio::ip::tcp::endpoint tcpEndpoint(asio::ip::tcp::v4(), tcpPortToBind);
        m_tcpAcceptor.open(tcpEndpoint.protocol());
        m_tcpAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        m_tcpAcceptor.bind(tcpEndpoint);
        m_tcpAcceptor.listen();

        // 3. Setup Work Guards to keep threads alive
        spdlog::trace("Setting up ASIO work guards");
        m_tcpWorkGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(m_tcpContext));
        m_udpWorkGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(m_udpContext));

        // 4. Spin up dedicated threads
        spdlog::trace("Starting dedicated TCP and UDP ASIO threads");
        m_tcpThread = std::thread([this]() 
        { 
            m_tcpContext.run(); 
        });
        
        m_udpThread = std::thread([this]() 
        { 
            m_udpContext.run(); 
        });

        if (m_isServerMode)
        {
            spdlog::info("Server Initialized successfully. UDP: 50000, TCP: 50001.");
        }
        else
        {
            spdlog::debug("Client network initialized successfully.");
        }

        return true;
    }
    catch (const std::exception& errorException)
    {
        spdlog::critical("Failed to initialize network sockets: {}", errorException.what());
        return false;
    }
}

void TailscaleNetwork::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (!m_tcpAcceptor.is_open())
    {
        spdlog::warn("Attempted to poll TCP connections but the acceptor is not open");
        return;
    }

    spdlog::debug("Starting to poll TCP connections");
    startTcpAcceptor(requestHandler);
}

void TailscaleNetwork::startTcpAcceptor(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    m_tcpAcceptor.async_accept(asio::make_strand(m_tcpContext), [this, requestHandler](const asio::error_code& error, asio::ip::tcp::socket socket)
    {
        // Loop the acceptor first to ensure it always restarts
        auto loopAcceptor = [&]() 
        { 
            startTcpAcceptor(requestHandler); 
        };

        if (error)
        {
            spdlog::warn("TCP Async Accept Error: {}. Backing off...", error.message());
            // Fail-safe: Back off slightly if OS is out of file descriptors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            loopAcceptor();
            return;
        }

        std::string remoteIp = socket.remote_endpoint().address().to_string();
        spdlog::debug("Accepted new TCP connection from {}", remoteIp);

        auto newSession = std::make_shared<TcpSession>(std::move(socket));

        auto onMessage = [this, requestHandler](std::shared_ptr<TcpSession> activeSession, const std::string& payload)
        {
            spdlog::trace("TCP message received from endpoint. Length: {}", payload.length());
            
            size_t firstPipe  = payload.find('|');
            size_t secondPipe = payload.find('|', firstPipe + 1);
            std::string messageType = payload.substr(0, firstPipe);

            bool hasValidPipes = firstPipe != std::string::npos && secondPipe != std::string::npos;
            if (hasValidPipes && messageType == "LOGIN")
            {
                std::string clientUuid = payload.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                std::lock_guard<std::mutex> lock(m_sessionMutex);

                // ACTIVE OVERRIDE: Kill the ghost connection if it exists
                auto existing = m_activeSessions.find(clientUuid);
                if (existing != m_activeSessions.end())
                {
                    spdlog::warn("Ghost connection detected for UUID: {}. Overriding previous session.", clientUuid);
                    existing->second->closeSession();
                }

                activeSession->setClientUuid(clientUuid);
                m_activeSessions[clientUuid] = activeSession;
                spdlog::info("Client logged in successfully. Tracked UUID: {}", clientUuid);
            }

            // Pass the payload up to Application.cpp's logic
            std::string response = requestHandler(activeSession->getRemoteEndpoint(), payload);
            if (!response.empty())
            {
                spdlog::trace("Sending TCP response back to {}", activeSession->getRemoteEndpoint());
                activeSession->sendData(response);
            }
        };

        auto onDisconnect = [this](std::shared_ptr<TcpSession> disconnectedSession)
        {
            // Clean up map on disconnect or timeout
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            std::string clientUuid = disconnectedSession->getClientUuid();

            spdlog::debug("TCP disconnect event triggered for UUID: '{}'", clientUuid);

            bool isTracked = !clientUuid.empty()
                          && m_activeSessions.count(clientUuid)
                          && m_activeSessions[clientUuid] == disconnectedSession;

            if (!isTracked)
            {
                spdlog::trace("Untracked or already purged session disconnected.");
                return;
            }
            
            m_activeSessions.erase(clientUuid);
            spdlog::info("Session purged for UUID: {}", clientUuid);
        };

        newSession->start(onMessage, onDisconnect);
        loopAcceptor();
    });
}

int TailscaleNetwork::getLocalUdpPort()
{
    if (m_udpSocket.is_open())
    {
        return m_udpSocket.local_endpoint().port();
    }
    
    spdlog::warn("getLocalUdpPort called but socket is not open, returning 0");
    return 0;
}

void TailscaleNetwork::sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload)
{
    try
    {
        asio::ip::udp::endpoint targetEndpoint;
        
        {
            std::shared_lock<std::shared_mutex> readLock(m_endpointCacheMutex);
            auto mapIterator = m_endpointCache.find(targetIp);
            
            if (mapIterator != m_endpointCache.end())
            {
                targetEndpoint = mapIterator->second;
            }
            else
            {
                readLock.unlock();
                std::unique_lock<std::shared_mutex> writeLock(m_endpointCacheMutex);
                
                mapIterator = m_endpointCache.find(targetIp);
                if (mapIterator != m_endpointCache.end())
                {
                    targetEndpoint = mapIterator->second;
                }
                else
                {
                    asio::ip::udp::resolver resolver(m_udpContext);
                    std::string ip = targetIp;
                    std::string port = std::to_string(m_port);
                    
                    size_t colonPos = targetIp.find(':');
                    if (colonPos != std::string::npos) 
                    {
                        ip = targetIp.substr(0, colonPos);
                        port = targetIp.substr(colonPos + 1);
                    }

                    spdlog::info("Resolving and caching new UDP endpoint: {}:{}", ip, port);
                    auto endpoints = resolver.resolve(asio::ip::udp::v4(), ip, port);
                    targetEndpoint = *endpoints.begin();
                    
                    m_endpointCache[targetIp] = targetEndpoint;
                }
            }
        }

        m_udpSocket.send_to(asio::buffer(dataPayload), targetEndpoint);
    }
    catch (const std::exception& errorException)
    {
        spdlog::debug("Exception caught while attempting to send UDP data to {}: {}", targetIp, errorException.what());
    }
}

void TailscaleNetwork::sendData(const asio::ip::udp::endpoint& targetEndpoint, const std::vector<uint8_t>& dataPayload)
{
    try
    {
        m_udpSocket.send_to(asio::buffer(dataPayload), targetEndpoint);
    }
    catch (const std::exception& errorException)
    {
        spdlog::debug("Exception caught while sending raw UDP packet: {}", errorException.what());
    }
}

bool TailscaleNetwork::receiveData(NetworkPacket& outPacket)
{
    asio::ip::udp::endpoint senderEndpoint;
    
    try
    {
        if (m_udpSocket.available() > 0)
        {
            size_t bytesReceived = m_udpSocket.receive_from(asio::buffer(m_receiveBuffer), senderEndpoint);
            
            outPacket.senderEndpoint = senderEndpoint;
            outPacket.payload.assign(m_receiveBuffer.begin(), m_receiveBuffer.begin() + bytesReceived);
            
            return true;
        }
    }
    catch (const std::exception& errorException)
    {
        spdlog::error("Error receiving UDP packet: {}", errorException.what());
    }
    
    return false; 
}

bool TailscaleNetwork::connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage)
{
    try
    {
        spdlog::debug("Attempting to establish persistent TCP connection to {}", targetIp);
        asio::ip::tcp::resolver resolver(m_tcpContext);
        
        std::string ip = targetIp;
        std::string port = "50001"; 
        
        size_t colonPos = targetIp.find(':');
        if (colonPos != std::string::npos)
        {
            ip = targetIp.substr(0, colonPos);
            port = targetIp.substr(colonPos + 1);
        }

        spdlog::trace("Resolving persistent TCP target {}:{}", ip, port);
        auto endpoints = resolver.resolve(ip, port);
        asio::ip::tcp::socket socket(m_tcpContext);
        
        // Synchronous connect just for the initial handshake, then we hand it to the async session
        asio::connect(socket, endpoints);
        spdlog::info("Successfully connected to persistent TCP endpoint {}:{}", ip, port);
        
        m_clientSession = std::make_shared<TcpSession>(std::move(socket));
        m_clientSession->start(
            [onMessage](std::shared_ptr<TcpSession>, const std::string& payload)
            {
                spdlog::trace("Received payload on persistent TCP connection. Length: {}", payload.length());
                onMessage(payload);
            },
            [](std::shared_ptr<TcpSession>)
            {
                spdlog::info("Persistent TCP connection disconnected from server.");
            });
            
        return true;
    }
    catch (const std::exception& errorException)
    {
        spdlog::error("Failed to connect persistent TCP to {}: {}", targetIp, errorException.what());
        return false;
    }
}

void TailscaleNetwork::sendPersistentTcp(const std::string& payload)
{
    if (m_clientSession)
    {
        spdlog::trace("Sending {} bytes via persistent TCP connection", payload.length());
        m_clientSession->sendData(payload);
    }
    else
    {
        spdlog::warn("Attempted to send persistent TCP data, but no active client session exists");
    }
}

void TailscaleNetwork::broadcastTcp(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    
    spdlog::debug("Broadcasting TCP message to {} active sessions", m_activeSessions.size());
    
    for (const auto& [uuid, session] : m_activeSessions)
    {
        spdlog::trace("Broadcasting payload to UUID: {}", uuid);
        session->sendData(payload);
    }
}

void TailscaleNetwork::sendTcpTo(const std::string& uuid, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    auto iterator = m_activeSessions.find(uuid);
    
    if (iterator != m_activeSessions.end())
    {
        spdlog::trace("Sending targeted TCP message to UUID: {}", uuid);
        iterator->second->sendData(payload);
    }
    else
    {
        spdlog::warn("Attempted to send targeted TCP message to unknown UUID: {}", uuid);
    }
}