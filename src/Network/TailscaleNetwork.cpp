#include "Network/TailscaleNetwork.hpp"


TailscaleNetwork::TailscaleNetwork()
    : m_ioContext(), m_udpSocket(m_ioContext), m_tcpAcceptor(m_ioContext), m_isServerMode(false)
{
    m_port = 50000; 
}

TailscaleNetwork::~TailscaleNetwork()
{
    if (m_udpSocket.is_open())
    {
        m_udpSocket.close();
    }
    if (m_tcpAcceptor.is_open())
    {
        m_tcpAcceptor.close();
    }
}

bool TailscaleNetwork::initialize(bool isServerMode)
{
    m_isServerMode = isServerMode;
    try
    {
        m_udpSocket.open(asio::ip::udp::v4());
        
        // Server gets 50000, Client gets 0 (OS assigned)
        unsigned short udpPortToBind = m_isServerMode ? 50000 : 0;
        asio::ip::udp::endpoint localUdpEndpoint(asio::ip::udp::v4(), udpPortToBind);
        m_udpSocket.bind(localUdpEndpoint);
        
        unsigned short tcpPortToBind = m_isServerMode ? 50001 : 0;
        asio::ip::tcp::endpoint tcpEndpoint(asio::ip::tcp::v4(), tcpPortToBind);
        m_tcpAcceptor.open(tcpEndpoint.protocol());
        m_tcpAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        m_tcpAcceptor.bind(tcpEndpoint);
        m_tcpAcceptor.listen();
        m_tcpAcceptor.non_blocking(true);
        
        if (m_isServerMode) 
        {
            std::cout << "TailscaleServer initialized. Bound to UDP 50000, TCP 50001." << std::endl;
        } 
        else
        {
            std::cout << "TailscaleClient initialized. Bound to TCP " << m_tcpAcceptor.local_endpoint().port() 
                      << " and UDP " << m_udpSocket.local_endpoint().port() << std::endl;
        }

        return true;
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Failed to initialize network sockets: " << errorException.what() << std::endl;
        return false;
    }
}

int TailscaleNetwork::getLocalTcpPort()
{
    if (m_tcpAcceptor.is_open())
    {
        return m_tcpAcceptor.local_endpoint().port();
    }
    return 0;
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
        asio::ip::udp::resolver resolver(m_ioContext);
        
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

std::string TailscaleNetwork::sendSynchronousTcp(const std::string& targetIp, const std::string& payload)
{
    try 
    {
        // Use a localized context so we can cleanly control the run loop and timeouts
        asio::io_context localIoContext;
        asio::ip::tcp::socket socket(localIoContext);
        asio::ip::tcp::resolver resolver(localIoContext);
        
        std::string ip = targetIp;
        std::string port = "50001"; 
        
        size_t colonPos = targetIp.find(':');
        if (colonPos != std::string::npos)
        {
            ip = targetIp.substr(0, colonPos);
            port = targetIp.substr(colonPos + 1);
        }

        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(ip, port);
        
        asio::error_code operationError;
        asio::steady_timer timeoutTimer(localIoContext);

        // 1. Connect with timeout
        timeoutTimer.expires_after(std::chrono::milliseconds(2000));
        timeoutTimer.async_wait([&socket](const asio::error_code& ec)
        {
            if (!ec)
            {
                asio::error_code ignoreError;
                socket.close(ignoreError);
            }
        });
        
        asio::async_connect(socket, endpoints, [&](const asio::error_code& ec, const asio::ip::tcp::endpoint&)
        {
            operationError = ec;
            timeoutTimer.cancel();
        });
        
        localIoContext.run();

        if (operationError || !socket.is_open())
        {
            return "";
        }

        // 2. Write with timeout
        localIoContext.restart();
        timeoutTimer.expires_after(std::chrono::milliseconds(2000));
        timeoutTimer.async_wait([&socket](const asio::error_code& ec)
        {
            if (!ec)
            {
                asio::error_code ignoreError;
                socket.close(ignoreError);
            }
        });
        
        asio::async_write(socket, asio::buffer(payload), [&](const asio::error_code& ec, std::size_t)
        {
            operationError = ec;
            timeoutTimer.cancel();
        });
        
        localIoContext.run();

        if (operationError)
        {
            return "";
        }

        // 3. Read with timeout
        localIoContext.restart();
        timeoutTimer.expires_after(std::chrono::milliseconds(2000));
        timeoutTimer.async_wait([&socket](const asio::error_code& ec)
        {
            if (!ec)
            {
                asio::error_code ignoreError;
                socket.close(ignoreError);
            }
        });
        
        char responseBuffer[8192] = {0};
        size_t length = 0;
        socket.async_read_some(asio::buffer(responseBuffer), [&](const asio::error_code& ec, std::size_t bytesRead)
        {
            operationError = ec;
            length = bytesRead;
            timeoutTimer.cancel();
        });
        
        localIoContext.run();

        asio::error_code shutdownError;
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, shutdownError);
        socket.close(shutdownError);
        
        if (!operationError || operationError == asio::error::eof) 
        {
            return std::string(responseBuffer, length);
        }
    } 
    catch (const std::exception& networkException) 
    {
        std::cerr << "TCP Sync Error: " << networkException.what() << std::endl;
    }
    
    return "";
}

void TailscaleNetwork::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (!m_tcpAcceptor.is_open())
    {
        return;
    }
    
    try 
    {
        asio::error_code ec;
        asio::ip::tcp::socket socket(m_ioContext);
        m_tcpAcceptor.accept(socket, ec);
        
        if (!ec) 
        {
            char data[4096];
            size_t length = socket.read_some(asio::buffer(data), ec);
            std::string incomingIp = socket.remote_endpoint().address().to_string();
            
            if (!ec || ec == asio::error::eof) 
            {
                std::string request(data, length);
                std::string response = requestHandler(incomingIp, request);
                
                if (!response.empty()) 
                {
                    asio::write(socket, asio::buffer(response));
                }
            }
            socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            socket.close();
        }
    } 
    catch (...) 
    {
    }
}

void TailscaleNetwork::waitForEvents(int timeoutMs)
{
    if (m_udpSocket.is_open())
    {
        m_udpSocket.async_wait(asio::socket_base::wait_read, [](const asio::error_code& ec)
        {
        });
    }

    if (m_tcpAcceptor.is_open())
    {
        m_tcpAcceptor.async_wait(asio::socket_base::wait_read, [](const asio::error_code& ec)
        {
        });
    }

    // Run the IO context. It will yield to the OS and block until either
    // a socket becomes readable, or the timeout expires.
    m_ioContext.restart();
    m_ioContext.run_for(std::chrono::milliseconds(timeoutMs));

    // Cancel pending waits so they do not conflict with the next manual poll.
    asio::error_code ec;
    if (m_udpSocket.is_open())
    {
        m_udpSocket.cancel(ec);
    }
    
    if (m_tcpAcceptor.is_open())
    {
        m_tcpAcceptor.cancel(ec);
    }
}