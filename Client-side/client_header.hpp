#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>

// Define buffer size for network communication
#define BUFFER_SIZE 1024
#define Logging_username true

enum class registerType
{
    Normal,
    Error_101,
};

class TcpClient{
private:
    int client_fd{};              // Socket file descriptor for client connection
    std::string client_ip;        // IP address of the client
    int port;                   // Port number for connection
    std::string server_ip{};      // IP address of the server to connect to
    sockaddr_in address_of_server{};  // Server address structure for connection
    
public:
    // Username for chat identification (default prefix for commands)
    std::string username{"/username "};
    
    // Buffer for receiving data from server
    char buffer[BUFFER_SIZE]{};
    /*
    - Try to register the user and your username
    - @param server_socket need to send to the server
    - @param type he defines with is occurred a error when we was send the username 0 for normal or 1 if occurred a error and we are send again
    */
    void register_user(int server_socket, registerType type);
    std::string register_username();
    // Constructor: Initialize client with port and optional IP address
    TcpClient(int _port, const char* client_ip = "127.0.0.1");
    /*
    - Verify and handle connection errors based on error code
    -@param error_code Error code returned from connection attempt normally is errno
    */
    int verify_error_connection(int error_code);
    // Connect to a TCP server at the specified IPv4 address
    int connect_to_server(const char* server_ipv4_address = "127.0.0.1");
    
    // Getter methods for client properties
    const char* getClientIp() const { return client_ip.c_str(); }
    int getClientFd() const { return client_fd; }
    int getPort() const { return port; }
    
    // Destructor: Clean up and close socket connection
    ~TcpClient() {
        close(client_fd);
    }
};
inline int TcpClient::connect_to_server(const char* server_ipv4_address)
{
    // Store server IP for reference
    server_ip = server_ipv4_address;
    
    // Validate IP address format
    if (inet_addr(server_ipv4_address) == INADDR_NONE) {
        std::cerr << "Invalid IP address: " << server_ipv4_address << std::endl;
        close(client_fd);
        return -1;
    }
    
    // Configure server address structure
    address_of_server.sin_family = AF_INET;
    address_of_server.sin_addr.s_addr = inet_addr(server_ipv4_address);
    address_of_server.sin_port = htons(port);
    
    // Attempt to connect to the server
    if (connect(client_fd, (struct sockaddr*)&address_of_server, sizeof(address_of_server)) == -1) {
        close(client_fd);
        return verify_error_connection(errno);
    }
    
    return 0; // Success
}

inline int TcpClient::verify_error_connection(int error_code)
{
    switch (error_code) {
        case ECONNREFUSED:
            std::cerr << "Connection refused by server at " << server_ip << ":" << port << std::endl;
            break;
            
        case ETIMEDOUT:
            std::cerr << "Connection to server at " << server_ip << ":" << port << " timed out." << std::endl;
            break;
            
        case EHOSTUNREACH:
            std::cerr << "No route to host " << server_ip << ":" << port << std::endl;
            break;
            
        case ENETUNREACH:
            std::cerr << "Network unreachable for " << server_ip << ":" << port << std::endl;
            break;
            
        default:
            std::cerr << "Failed to connect to " << server_ip << ":" << port 
                      << " - Error: " << strerror(error_code) << std::endl;
            break;
    }
    
    return -1;
}

inline void TcpClient::register_user(int server_socket, registerType type)
{
    if (type == registerType::Error_101)
    {
        std::cout << "[Error101] This username is already in use\n";
    }
    
    username = register_username();
    std::string greeting = "/username " + username + "\n";
    send(server_socket, greeting.c_str(), greeting.size(),0);
}



/**
 * Constructor: Creates a TCP client socket
 * @param _port Port number to use for connection
 * @param client_ip IP address of the client (default: localhost)
 * @throws std::runtime_error if socket creation fails
 */
inline TcpClient::TcpClient(int _port, const char* client_ip)
    : port(_port), client_ip(client_ip)
{
    // Create a TCP socket (IPv4, stream-based, default protocol)
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Check if socket creation was successful
    if (client_fd == -1) {
        throw std::runtime_error("Socket creation failed");
    }
}

/**
 * Establishes connection to the server
 * @param server_ipv4_address IPv4 address of the server (default: localhost)
 * @throws std::runtime_error if connection fails
 */

inline std::string TcpClient::register_username()
{
    bool IsRegistered = false;
    std::string name;
    // Prompt for username
    std::cout << "Enter your username: ";
    std::cout.flush();
    
    // Read username using standard input (canonical mode)
    // This happens before setup_stdin() is called, so input is line-buffered
    std::getline(std::cin, name);
    
    // Validate username - ensure it's not empty and has reasonable length
    while (name.empty() || name.length() > 50)
    {
        if (name.empty())
            std::cout << "Username cannot be empty. Please try again: ";
        else
            std::cout << "Username too long (max 50 chars). Please try again: ";
        
        std::cout.flush();
        std::getline(std::cin, name);
    }
    
    // Remove any leading/trailing whitespace
    name.erase(0, name.find_first_not_of(" \t\n\r"));
    name.erase(name.find_last_not_of(" \t\n\r") + 1);
    
    return name;
}