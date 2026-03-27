#include "Network/TailscaleNetwork.hpp"
#include <iostream>

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
        
        if (m_isServerMode) 
        {
            asio::ip::udp::endpoint localEndpoint(asio::ip::udp::v4(), m_port);
            m_udpSocket.bind(localEndpoint);
            
            asio::ip::tcp::endpoint tcpEndpoint(asio::ip::tcp::v4(), 50001);
            m_tcpAcceptor.open(tcpEndpoint.protocol());
            m_tcpAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            m_tcpAcceptor.bind(tcpEndpoint);
            m_tcpAcceptor.listen();
            m_tcpAcceptor.non_blocking(true);
            
            std::cout << "TailscaleServer initialized. Bound to UDP 50000, TCP 50001." << std::endl;
        } 
        else 
        {
            std::cout << "TailscaleClient initialized." << std::endl;
        }
        return true;
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Failed to initialize network sockets: " << errorException.what() << std::endl;
        return false;
    }
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
        asio::ip::tcp::socket socket(m_ioContext);
        asio::ip::tcp::resolver resolver(m_ioContext);
        
        std::string ip = targetIp;
        size_t colonPos = targetIp.find(':');
        if (colonPos != std::string::npos)
        {
            ip = targetIp.substr(0, colonPos);
        }

        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(ip, "50001");
        asio::connect(socket, endpoints);
        asio::write(socket, asio::buffer(payload));
        
        char response[8192];
        asio::error_code ec;
        size_t length = socket.read_some(asio::buffer(response), ec);
        
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket.close();
        
        if (!ec || ec == asio::error::eof) 
        {
            return std::string(response, length);
        }
    } 
    catch (...) 
    {
    }
    
    return "";
}

void TailscaleNetwork::pollTcpConnections(std::function<std::string(const std::string&, const std::string&)> requestHandler)
{
    if (!m_isServerMode || !m_tcpAcceptor.is_open())
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
    // Queue an asynchronous wait operation without consuming the data.
    if (m_udpSocket.is_open())
    {
        m_udpSocket.async_wait(asio::socket_base::wait_read, [](const asio::error_code& ec)
        {
        });
    }

    if (m_isServerMode && m_tcpAcceptor.is_open())
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
    
    if (m_isServerMode && m_tcpAcceptor.is_open())
    {
        m_tcpAcceptor.cancel(ec);
    }
}