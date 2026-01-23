#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <array>
#include <sys/select.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unordered_map>
#include <unordered_set>
#include <string>

// Maximum size for network data buffer
#define BUFFER_SIZE 1024
#define DUPLICATED_USERNAME_ERROR "101"


// Maximum number of simultaneous client connections allowed
#define MAX_CLIENTS 7


/**
 * TcpServer class - Handles multiple client connections using select()
 * This server can handle multiple clients simultaneously without threading
 * by using I/O multiplexing with the select() system call
 */
class TcpServer{ 
public:
    /**
     * Constructor: Initializes and starts the TCP server
     * @param _port Port number to listen on
     * @param ipv4_address IP address to bind to (default: localhost)
     */
    TcpServer(int _port, const char* ipv4_address = "127.0.0.1");
    
    /**
     * Initializes file descriptor sets for monitoring client connections
     * Sets up master_fds with the server socket for accepting new connections
     */
    void setupClientSockets();
    
    /**
     * Gracefully stops the server loop
     * Sets the SERVER_IS_RUNNING flag to false, allowing run() to exit cleanly
     */
    void Shutting_down();
    
    /**
     * Signal handler for graceful shutdown (e.g., Ctrl+C)
     * @param signum Signal number received (e.g., SIGINT)
     */
    void signalHandler(int signum) {
        std::cout << "\nReceived signal " << signum << " (Ctrl+C). Shutting down...\n";
        SERVER_IS_RUNNING = false;  // Stop the server loop gracefully
    }
    
    /**
     * Main server loop - monitors and handles all client connections
     * Uses select() to efficiently handle multiple clients:
     * - Accepts new connections when clients try to connect
     * - Reads data from clients that send messages
     * - Broadcasts messages to all other connected clients
     */
    void run();
    
    /**
     * Destructor: Cleans up all connections and closes server socket
     */
    ~TcpServer();
    
    // Getter methods for server properties
    int getServerFd() const { return server_fd; }
    int getPort() const { return port; }
    
    /**
     * Client structure - stores information about each connected client
     * fd: File descriptor (socket) for this client connection
     * username: Display name for chat messages
     */
    struct Client {
        int fd;              // Socket file descriptor for this client
        std::string client_buffer{};
        std::string username{};
    };
    /*
    - This is a username stored with it, we can easily check if new users have the same username as already registered users
    */
    std::unordered_set<std::string> usernames;
    bool IsDuplicated_Username(const char* username);
    // Temporary buffer for parsing username from client messages
    char tempUsername[64]{};
    /*
    - Disconnect client from the server.
    */
    void disconnect_client(int client_fd);
    /**
     * Map of all connected clients
     * Key: file descriptor (socket ID)
     * Value: Client struct with fd and username
     * Allows quick lookup and iteration over all active connections
     */
    std::unordered_map<int, Client> clients{}; 

private:
    /**
     * Server running state flag
     * When false, the main loop in run() will exit, shutting down the server
     */
    bool SERVER_IS_RUNNING{true};
    
    /**
     * Buffer for receiving data from clients
     * Reused for each recv() call to avoid repeated allocations
     */
    char buffer[BUFFER_SIZE]{};
    
    /**
     * Server socket file descriptor
     * This socket listens for incoming connection requests
     * It's added to master_fds and monitored by select()
     */
    int server_fd{};
    
    /**
     * Maximum file descriptor number
     * Used by select() to know the range of file descriptors to check
     * Updated whenever a new client connects with a higher fd number
     */
    int max_fd{};
    
    /**
     * Port number the server is listening on
     * Specified during construction
     */
    int port{};
    
    /**
     * Working set of file descriptors for select()
     * This set is modified by select() to indicate which sockets have data ready
     * Copied from master_fds before each select() call since select() modifies it
     */
    fd_set read_fds{};
    
    /**
     * Master set of all active file descriptors
     * Contains:
     * - server_fd: for accepting new connections
     * - All client socket fds: for receiving messages
     * This set persists across select() calls and is the source of truth
     */
    fd_set master_fds{}; 
};