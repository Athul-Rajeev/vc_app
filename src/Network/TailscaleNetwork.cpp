#include "Network/TailscaleNetwork.hpp"

TailscaleNetwork::TailscaleNetwork()
    : m_ioContext(), m_udpSocket(m_ioContext)
{
    // Port 50000 is generally unassigned and safe for custom VoIP usage
    m_port = 50000; 
}

TailscaleNetwork::~TailscaleNetwork()
{
    if (m_udpSocket.is_open())
    {
        m_udpSocket.close();
    }
}

bool TailscaleNetwork::initialize()
{
    try
    {
        // Open the socket for IPv4 UDP communication
        m_udpSocket.open(asio::ip::udp::v4());
        
        // Bind to any available network interface on the specified port.
        // This will automatically listen on your Tailscale IP as well as your local LAN IP.
        asio::ip::udp::endpoint localEndpoint(asio::ip::udp::v4(), m_port);
        m_udpSocket.bind(localEndpoint);
        
        std::cout << "TailscaleNetwork initialized. Bound to port: " << m_port << std::endl;
        return true;
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Failed to initialize UDP socket: " << errorException.what() << std::endl;
        return false;
    }
}

void TailscaleNetwork::sendData(const std::string& targetIp, const std::vector<uint8_t>& dataPayload)
{
    try
    {
        asio::ip::udp::resolver resolver(m_ioContext);
        
        // Resolve the target Tailscale IP and port
        asio::ip::udp::resolver::results_type endpoints = resolver.resolve(asio::ip::udp::v4(), targetIp, std::to_string(m_port));
        
        // Send the raw byte payload to the first resolved endpoint
        m_udpSocket.send_to(asio::buffer(dataPayload), *endpoints.begin());
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Error sending packet: " << errorException.what() << std::endl;
    }
}

std::vector<uint8_t> TailscaleNetwork::receiveData()
{
    // A standard Maximum Transmission Unit (MTU) size for UDP to prevent packet fragmentation
    std::vector<uint8_t> bufferData(1500); 
    asio::ip::udp::endpoint senderEndpoint;
    
    try
    {
        // Only attempt to read if there is actually data waiting in the OS network buffer
        if (m_udpSocket.available() > 0)
        {
            size_t bytesReceived = m_udpSocket.receive_from(asio::buffer(bufferData), senderEndpoint);
            
            // Shrink the vector to the exact size of the received packet
            bufferData.resize(bytesReceived);
            return bufferData;
        }
    }
    catch (const std::exception& errorException)
    {
        std::cerr << "Error receiving packet: " << errorException.what() << std::endl;
    }
    
    // Return an empty vector if no data was waiting
    return std::vector<uint8_t>(); 
}