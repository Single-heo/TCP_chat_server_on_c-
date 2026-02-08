#include "server-header.hpp"
#include "../../USEFULL-HEADERS/input.hpp"

/**
 * Constructor: Initializes the TCP server and sets up the listening socket
 * 
 * @param _port Port number to bind the server to (e.g., 25565)
 * @param ipv4_address IP address to bind to (default: "127.0.0.1" for localhost)
 * 
 * Steps:
 * 1. Ignore SIGPIPE signal (prevents server crash when client disconnects)
 * 2. Create TCP socket
 * 3. Set socket options (SO_REUSEADDR allows quick restart)
 * 4. Bind socket to IP:port
 * 5. Start listening for connections
 */
TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    // Ignore SIGPIPE to prevent server crash when writing to closed sockets
    // Without this, writing to a disconnected client would terminate the server
    signal(SIGPIPE, SIG_IGN);
    
    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;
    
    // Create a TCP socket (SOCK_STREAM = TCP, AF_INET = IPv4)
    // Returns a file descriptor that represents this socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation error" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }
    
    // Enable SO_REUSEADDR option:
    // Allows the server to bind to a port that was recently used
    // Without this, you'd get "Address already in use" error when restarting quickly
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    }
    
    // Configure the server address structure
    sockaddr_in address;
    address.sin_family = AF_INET;                       // IPv4 protocol
    address.sin_addr.s_addr = inet_addr(ipv4_address);  // Convert IP string to binary
    address.sin_port = htons(port);                     // Convert port to network byte order (big-endian)
    
    // Bind the socket to the specified IP address and port
    // This associates the socket with a specific network interface and port number
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_fd);
        throw std::runtime_error("Bind failed");
    }
    
    // Start listening for incoming connections
    // The '3' is the backlog queue size - max number of pending connections
    if(listen(server_fd, 3) == -1) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        throw std::runtime_error("Listen failed");
    }
    
    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

/**
 * Initialize the epoll instance and add the server socket to it
 * 
 * epoll is a scalable I/O event notification mechanism (better than select/poll)
 * It allows monitoring multiple file descriptors efficiently
 * 
 * Steps:
 * 1. Create epoll instance with epoll_create1()
 * 2. Configure event structure for server socket
 * 3. Add server socket to epoll (monitor for incoming connections)
 */
void TcpServer::initialize_epoll()
{
    // Create an epoll instance
    // The '0' flag means use default settings
    // Returns a file descriptor representing the epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    
    // Configure the epoll event structure for the server socket
    struct epoll_event ev;
    ev.events = EPOLLIN;      // EPOLLIN = monitor for readable data (incoming connections)
    ev.data.fd = server_fd;   // Store the server socket fd in the event data
                              // This allows us to identify which fd triggered when epoll_wait returns
    
    // Add the server socket to the epoll instance
    // EPOLL_CTL_ADD = add a new file descriptor to monitor
    // Now epoll will notify us when new connections arrive on server_fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }
    
    std::cout << "Epoll initialized successfully\n";
}

/**
 * Add a file descriptor to the epoll instance for monitoring
 * 
 * @param fd The file descriptor to monitor (usually a client socket)
 * @param events Bitmask of events to monitor (EPOLLIN, EPOLLOUT, etc.)
 * 
 * Common events:
 * - EPOLLIN: Data available to read (incoming messages)
 * - EPOLLOUT: Socket ready for writing (can send data)
 * - EPOLLERR: Error condition
 * - EPOLLHUP: Hang up (connection closed)
 */
void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    // Create a temporary event structure on the stack
    // This is safe because epoll_ctl copies the data internally
    struct epoll_event ev;
    ev.events = events;    // What events to monitor (e.g., EPOLLIN for reading)
    ev.data.fd = fd;       // Store the fd so we know which socket triggered
    
    // Add this fd to the epoll instance
    // After this call, epoll will notify us when the specified events occur on this fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
    }
}

/**
 * Remove a file descriptor from epoll monitoring
 * 
 * @param fd The file descriptor to stop monitoring
 * 
 * Important: The event parameter is IGNORED for EPOLL_CTL_DEL operations
 * The kernel uses the fd as the unique identifier, not the event struct
 * Each fd can only be registered once in an epoll instance
 */
void TcpServer::remove_from_epoll(int fd)
{
    // EPOLL_CTL_DEL = remove a file descriptor from epoll monitoring
    // The last parameter (event) is ignored for DEL operations, so we pass nullptr
    // The kernel identifies which entry to remove based solely on the fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl: del");
    }
}

/**
 * Handle a new incoming client connection
 * 
 * Called when epoll notifies us that the server socket has data ready (EPOLLIN)
 * This means a client is trying to connect
 * 
 * Steps:
 * 1. Accept the connection (creates a new socket for this client)
 * 2. Add the new client socket to epoll monitoring
 * 3. Create a Client entry in our clients map
 */
void TcpServer::handle_new_connection()
{
    // Prepare structure to receive client address information
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    
    // Accept the pending connection
    // This creates a NEW socket (new_fd) specifically for this client
    // The server_fd continues listening for other connections
    // client_addr will be filled with the client's IP and port
    int new_fd = accept(server_fd, (sockaddr *)&client_addr, &len);
    if (new_fd < 0) {
        perror("accept");
        return;  // Connection failed, but server continues running
    }
    
    // Optional: Make socket non-blocking for edge-triggered epoll (EPOLLET)
    // Non-blocking means recv/send won't wait if no data is available
    // Commented out because we're using level-triggered mode (default)
    // int flags = fcntl(new_fd, F_GETFL, 0);
    // fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Add the new client socket to epoll monitoring
    // EPOLLIN = notify us when this client sends data
    add_to_epoll(new_fd, EPOLLIN);
    
    // Create a Client struct to track this client's state
    // fd: the socket file descriptor
    // username: empty initially, set when client registers
    // client_buffer: unused in current implementation
    // write_buffer: accumulates partial messages until we receive a newline
    clients[new_fd] = Client{new_fd, "", "", ""};
    
    std::cout << "Client connected fd=" << new_fd << std::endl;
}

/**
 * Gracefully stop the server
 * 
 * Sets the flag that controls the main loop in run()
 * The server will finish processing current events and then exit cleanly
 */
void TcpServer::Shutting_down()
{
    SERVER_IS_RUNNING = false;
}

/**
 * Main server loop - the heart of the server
 * 
 * This function runs continuously, monitoring all sockets using epoll
 * and handling events as they occur:
 * - New connections on server_fd
 * - Incoming messages from clients
 * - Client disconnections
 * 
 * epoll_wait() blocks until at least one event occurs or timeout expires
 */
void TcpServer::run()
{
    // Set up epoll and add server socket to monitoring
    initialize_epoll();
    
    // Array to receive events from epoll_wait()
    // epoll_wait() fills this array with ready file descriptors
    // MAX_EVENTS (10) is the maximum number of events we can process per iteration
    struct epoll_event events[MAX_EVENTS];
    
    std::cout << "Server running with epoll...\n";

    // Main event loop - runs until Shutting_down() is called
    while (SERVER_IS_RUNNING)
    {
        // Wait for events on any monitored file descriptor
        // Parameters:
        // - epoll_fd: our epoll instance
        // - events: array to fill with ready file descriptors
        // - MAX_EVENTS: maximum number of events to return
        // - 1000: timeout in milliseconds (1 second)
        //
        // Returns: number of ready file descriptors (nfds)
        // The events array is filled with nfds entries
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        
        // Error handling
        if (nfds < 0) {
            // EINTR = interrupted by signal (e.g., Ctrl+C) - not a real error
            if (errno == EINTR)
                continue;  // Try again
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;  // Fatal error, exit loop
        }
        
        // Timeout occurred (no events in 1 second)
        // This allows us to check SERVER_IS_RUNNING periodically
        if (nfds == 0)
            continue;  // Go back to epoll_wait
        
        // Process all ready file descriptors returned by epoll_wait
        // nfds tells us how many events occurred
        for (int i = 0; i < nfds; i++)
        {
            // Extract the file descriptor that has data ready
            // We stored this in ev.data.fd when we added it to epoll
            int fd = events[i].data.fd;
            
            // Case 1: Activity on server socket = new connection request
            if (fd == server_fd) {
                handle_new_connection();
                continue;  // Move to next event
            }
            
            // Case 2: Activity on client socket = incoming data from client
            
            // Clear buffer before receiving new data
            memset(buffer, 0, BUFFER_SIZE);
            
            // Receive data from the client
            // Parameters:
            // - fd: client socket
            // - buffer: where to store received data
            // - BUFFER_SIZE - 1: max bytes to read (leave room for null terminator)
            // - 0: flags (none)
            //
            // Returns: number of bytes received (n)
            ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);
            
            // Handle disconnection or error
            if (n <= 0) {
                if (n == 0)
                    std::cout << "Client disconnected fd=" << fd << std::endl;  // Graceful close
                else
                    perror("recv");  // Error occurred
                
                disconnect_client(fd);
                continue;  // Move to next event
            }
            
            // Check if message has a newline (complete message)
            // Messages are delimited by '\n' in this protocol
            bool has_newline = bufferEndsWith(buffer, n, "\n");
            
            // Clean up the buffer (remove trailing whitespace, newlines, etc.)
            n = trimBuffer(buffer, n);
            
            // Ignore empty messages
            if (isBufferEmpty(buffer, n))
                continue;
            
            // Parse username registration command
            // Protocol: /username <name>\n
            // parse_username extracts the username into tempUsername
            if (parse_username(buffer, tempUsername, sizeof(tempUsername)))
            {
                // Check if username is already taken
                if (IsDuplicated_Username(tempUsername)) {
                    // Send error code "101" to client
                    send(fd, DUPLICATED_USERNAME_ERROR, strlen(DUPLICATED_USERNAME_ERROR), 0);
                    memset(buffer, 0, BUFFER_SIZE);
                    continue;
                }
                
                // Register the username
                clients[fd].username = tempUsername;
                usernames.insert(tempUsername);  // Add to set for duplicate checking
                
                // Send success confirmation to client
                const char* success_msg = "OK\n";
                send(fd, success_msg, strlen(success_msg), 0);
                
                continue;  // Done processing username registration
            }
            
            // Accumulate message data in write_buffer
            // TCP is a stream protocol - messages may arrive in chunks
            // We need to buffer partial messages until we get a complete one
            clients[fd].write_buffer += buffer;
            
            // If no newline yet, we don't have a complete message
            // Keep accumulating and wait for more data
            if (!has_newline)
                continue;
            
            // We have a complete message - broadcast it to all other clients
            // Format: "username: message\n"
            std::string msg = clients[fd].username + ": " + 
                            clients[fd].write_buffer + "\n";
            
            // Clear the buffer now that we've used the message
            clients[fd].write_buffer.clear();
            
            // Broadcast to all connected clients except the sender
            // Using structured bindings (C++17): [fd, data] = pair<int, Client>
            for (auto& [client_fd, client_data] : clients)
            {
                // Don't send message back to the sender
                if (client_fd == fd)
                    continue;
                
                // Send the message to this client
                // Returns: number of bytes sent, or -1 on error
                ssize_t sent = send(client_fd, msg.c_str(), msg.size(), 0);
                
                // If send fails, the client likely disconnected
                if (sent <= 0) {
                    perror("send");
                    disconnect_client(client_fd);
                    // Note: This modifies the map we're iterating over
                    // This is safe in C++ because we break/continue immediately
                }
            }
        }
    }
}

/**
 * Destructor: Clean up all resources when server shuts down
 * 
 * Steps:
 * 1. Close all client connections
 * 2. Close the epoll instance
 * 3. Close the server socket
 */
TcpServer::~TcpServer()
{
    // Close all client sockets
    // Using structured bindings to iterate over the map
    for (auto& [fd, client] : clients) {
        close(fd);  // Close the socket connection
    }
    
    // Close the epoll file descriptor
    close(epoll_fd);
    
    // Close the server socket (stop listening for new connections)
    close(server_fd);
    
    std::cout << "Server shut down successfully" << std::endl;
}

/**
 * Check if a username is already registered
 * 
 * @param new_username Username to check
 * @return true if username exists, false otherwise
 * 
 * Uses std::unordered_set for O(1) average lookup time
 */
bool TcpServer::IsDuplicated_Username(const char *new_username)
{
    // count() returns 1 if found, 0 if not found
    return usernames.count(new_username) > 0;
}

/**
 * Disconnect a client and clean up all associated resources
 * 
 * @param client_fd File descriptor of the client to disconnect
 * 
 * Steps:
 * 1. Remove from epoll monitoring (stop getting notifications)
 * 2. Close the socket connection
 * 3. Remove username from the set
 * 4. Remove client entry from the map
 */
void TcpServer::disconnect_client(int client_fd)
{
    // Stop monitoring this socket in epoll
    // Important: Do this BEFORE closing the socket
    remove_from_epoll(client_fd);
    
    // Close the socket connection
    close(client_fd);
    
    // Find the client in our map
    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        // Remove their username from the set (makes it available for others)
        usernames.erase(it->second.username);
        
        // Remove the client entry from the map
        clients.erase(it);
    }
}