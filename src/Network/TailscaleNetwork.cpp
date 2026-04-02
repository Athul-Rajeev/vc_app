#include "Network/TailscaleNetwork.hpp"
#include <iostream>

TailscaleNetwork::TailscaleNetwork()
    : m_tcpContext(), 
      m_udpContext(),
      m_udpSocket(m_udpContext), 
      m_tcpAcceptor(m_tcpContext), 
      m_isServerMode(false)
{
    m_port = 50000;
}

TailscaleNetwork::~TailscaleNetwork()
{
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
}

bool TailscaleNetwork::initialize(bool isServerMode)
{
    m_isServerMode = isServerMode;

    try
    {
        // 1. Initialize UDP (Data Plane)
        m_udpSocket.open(asio::ip::udp::v4());
        unsigned short udpPortToBind = m_isServerMode ? 50000 : 0;
        asio::ip::udp::endpoint localUdpEndpoint(asio::ip::udp::v4(), udpPortToBind);
        m_udpSocket.bind(localUdpEndpoint);

        // 2. Initialize TCP (Control Plane)
        unsigned short tcpPortToBind = m_isServerMode ? 50001 : 0;
        asio::ip::tcp::endpoint tcpEndpoint(asio::ip::tcp::v4(), tcpPortToBind);
        m_tcpAcceptor.open(tcpEndpoint.protocol());
        m_tcpAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        m_tcpAcceptor.bind(tcpEndpoint);
        m_tcpAcceptor.listen();

        // 3. Setup Work Guards to keep threads alive
        m_tcpWorkGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(m_tcpContext));
        m_udpWorkGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(m_udpContext));

        // 4. Spin up dedicated threads
        m_tcpThread = std::thread([this]() { m_tcpContext.run(); });
        m_udpThread = std::thread([this]() { m_udpContext.run(); });

        if (m_isServerMode)
        {
            std::cout << "Server Initialized. UDP: 50000, TCP: 50001." << std::endl;
        }

        return true;
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Failed to initialize network sockets: " << errorException.what() << std::endl;
        return false;
    }
}

void TailscaleNetwork::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (!m_tcpAcceptor.is_open())
    {
        return;
    }

    startTcpAcceptor(requestHandler);
}

void TailscaleNetwork::startTcpAcceptor(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    m_tcpAcceptor.async_accept(asio::make_strand(m_tcpContext), [this, requestHandler](const asio::error_code& error, asio::ip::tcp::socket socket)
    {
        // Loop the acceptor first to ensure it always restarts
        auto loopAcceptor = [&]() { startTcpAcceptor(requestHandler); };

        if (error)
        {
            // Fail-safe: Back off slightly if OS is out of file descriptors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            loopAcceptor();
            return;
        }

        auto newSession = std::make_shared<TcpSession>(std::move(socket));

        auto onMessage = [this, requestHandler](std::shared_ptr<TcpSession> activeSession, const std::string& payload)
        {
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
                    std::cout << "Ghost connection detected for " << clientUuid << ". Overriding." << std::endl;
                    existing->second->closeSession();
                }

                activeSession->setClientUuid(clientUuid);
                m_activeSessions[clientUuid] = activeSession;
            }

            // Pass the payload up to Application.cpp's logic
            std::string response = requestHandler(activeSession->getRemoteEndpoint(), payload);
            if (!response.empty())
            {
                activeSession->sendData(response);
            }
        };

        auto onDisconnect = [this](std::shared_ptr<TcpSession> disconnectedSession)
        {
            // Clean up map on disconnect or timeout
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            std::string clientUuid = disconnectedSession->getClientUuid();

            bool isTracked = !clientUuid.empty()
                          && m_activeSessions.count(clientUuid)
                          && m_activeSessions[clientUuid] == disconnectedSession;

            if (!isTracked)
            {
                return;
            }
            m_activeSessions.erase(clientUuid);
            std::cout << "Session purged for UUID: " << clientUuid << std::endl;
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
    return 0;
}

void TailscaleNetwork::sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload)
{
    try
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

        asio::ip::udp::resolver::results_type endpoints = resolver.resolve(asio::ip::udp::v4(), ip, port);
        m_udpSocket.send_to(asio::buffer(dataPayload), *endpoints.begin());
    }
    catch (const std::exception& errorException)
    {
    }
}

NetworkPacket TailscaleNetwork::receiveData()
{
    std::vector<uint8_t> bufferData(1500); 
    asio::ip::udp::endpoint senderEndpoint;
    
    try
    {
        if (m_udpSocket.available() > 0)
        {
            size_t bytesReceived = m_udpSocket.receive_from(asio::buffer(bufferData), senderEndpoint);
            bufferData.resize(bytesReceived);
            
            NetworkPacket pack;
            pack.senderIp = senderEndpoint.address().to_string() + ":" + std::to_string(senderEndpoint.port());
            pack.payload = std::move(bufferData);
            return pack;
        }
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Error receiving packet: " << errorException.what() << std::endl;
    }
    
    return NetworkPacket{"", std::vector<uint8_t>()}; 
}

bool TailscaleNetwork::connectPersistentTcp(const std::string& targetIp, std::function<void(const std::string&)> onMessage)
{
    try
    {
        asio::ip::tcp::resolver resolver(m_tcpContext);
        
        std::string ip = targetIp;
        std::string port = "50001"; 
        
        size_t colonPos = targetIp.find(':');
        if (colonPos != std::string::npos)
        {
            ip = targetIp.substr(0, colonPos);
            port = targetIp.substr(colonPos + 1);
        }

        auto endpoints = resolver.resolve(ip, port);
        asio::ip::tcp::socket socket(m_tcpContext);
        
        // Synchronous connect just for the initial handshake, then we hand it to the async session
        asio::connect(socket, endpoints);
        
        m_clientSession = std::make_shared<TcpSession>(std::move(socket));
        m_clientSession->start(
            [onMessage](std::shared_ptr<TcpSession>, const std::string& payload)
            {
                onMessage(payload);
            },
            [](std::shared_ptr<TcpSession>)
            {
                std::cout << "Disconnected from server." << std::endl;
            });
            
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to connect persistent TCP: " << e.what() << std::endl;
        return false;
    }
}

void TailscaleNetwork::sendPersistentTcp(const std::string& payload)
{
    if (m_clientSession)
    {
        m_clientSession->sendData(payload);
    }
}

void TailscaleNetwork::broadcastTcp(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    for (const auto& [uuid, session] : m_activeSessions)
    {
        session->sendData(payload);
    }
}

void TailscaleNetwork::sendTcpTo(const std::string& uuid, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    auto iterator = m_activeSessions.find(uuid);
    if (iterator != m_activeSessions.end())
    {
        iterator->second->sendData(payload);
    }
}