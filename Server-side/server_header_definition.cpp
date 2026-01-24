#include "server-header.hpp"
#include "../../USEFULL-HEADERS/input.hpp"

TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    // Ignore SIGPIPE to prevent server crash when writing to closed sockets
    signal(SIGPIPE, SIG_IGN);
    
    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;
    
    // Create TCP socket for incoming connections
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation error" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }
    
    // Enable address reuse to avoid "Address already in use" errors
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    }
    
    // Configure server address structure
    sockaddr_in address;
    address.sin_family = AF_INET;                      // IPv4
    address.sin_addr.s_addr = inet_addr(ipv4_address); // Server IP
    address.sin_port = htons(port);                     // Port in network byte order
    
    // Bind socket to the specified address and port
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_fd);
        throw std::runtime_error("Bind failed");
    }
    
    // Start listening for incoming connections (backlog queue of 3)
    if(listen(server_fd, 3) == -1) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        throw std::runtime_error("Listen failed");
    }
    
    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

void TcpServer::setupClientSockets()
{
    // Initialize file descriptor sets
    FD_ZERO(&master_fds);  // Clear master set
    FD_ZERO(&read_fds);    // Clear read set
    
    // Add server socket to master set for monitoring new connections
    FD_SET(server_fd, &master_fds);
    
    // Initialize max_fd with server socket descriptor
    max_fd = server_fd;
}

/*
 * Power off the main loop
 * Sets the flag to stop the server gracefully
 */
void TcpServer::Shutting_down()
{
    SERVER_IS_RUNNING = false;
}

/*
 * Main server loop - handles incoming connections and client messages
 * Uses select() to monitor multiple file descriptors simultaneously:
 * - server_fd: for new connection requests
 * - client sockets: for incoming messages from connected clients
 */
void TcpServer::run()
{
    setupClientSockets();

    while (SERVER_IS_RUNNING)
    {
        read_fds = master_fds;

        timeval tv{};
        tv.tv_sec = 1;   // permite shutdown limpo
        tv.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);

        if (ready < 0)
        {
            if (errno == EINTR)
                continue;

            std::cerr << "select error: " << strerror(errno) << std::endl;
            break;
        }

        // timeout (nenhum fd pronto)
        if (ready == 0)
            continue;

        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (!FD_ISSET(fd, &read_fds))
                continue;

            /* =========================
               NOVA CONEXÃO
               ========================= */
            if (fd == server_fd)
            {
                sockaddr_in client_addr{};
                socklen_t len = sizeof(client_addr);

                int new_fd = accept(server_fd, (sockaddr *)&client_addr, &len);
                if (new_fd < 0)
                {
                    perror("accept");
                    continue;
                }

                FD_SET(new_fd, &master_fds);
                if (new_fd > max_fd)
                    max_fd = new_fd;

                clients[new_fd] = Client{
                    new_fd,
                    ""  // username vazio inicialmente
                };


                std::cout << "Client connected fd=" << new_fd << std::endl;
                continue;
            }

            /* =========================
               DADOS DE CLIENTE
               ========================= */
            memset(buffer, 0, BUFFER_SIZE);

            ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

            if (n <= 0)
            {
                if (n == 0)
                    std::cout << "Client disconnected fd=" << fd << std::endl;
                else
                    perror("recv");

                disconnect_client(fd);
                continue;
            }

            bool has_newline = bufferEndsWith(buffer, n, "\n");
            n = trimBuffer(buffer, n);

            if (isBufferEmpty(buffer, n))   
                continue;

            // username
            if (parse_username(buffer, tempUsername, sizeof(tempUsername)))
            {
                if (IsDuplicated_Username(tempUsername))
                {
                    send(fd, DUPLICATED_USERNAME_ERROR, strlen(DUPLICATED_USERNAME_ERROR), 0);
                    memset(buffer, 0, BUFFER_SIZE);
                    continue;   
                }
                clients[fd].username = tempUsername;
                usernames.insert(tempUsername);
                
                // ✅ ADD THESE LINES - Send confirmation to client
                const char* success_msg = "OK\n";
                send(fd, success_msg, strlen(success_msg), 0);
                
                continue;
            }

            clients[fd].client_buffer += buffer;

            if (!has_newline)
                continue;

            std::string msg =
                clients[fd].username + ": " +
                clients[fd].client_buffer + "\n";

            clients[fd].client_buffer.clear();

            /* =========================
               BROADCAST
               ========================= */
            for (auto it = clients.begin(); it != clients.end(); )
            {
                if (it->first == fd)
                {
                    ++it;
                    continue;
                }

                ssize_t s = send(it->first, msg.c_str(), msg.size(), 0);

                if (s <= 0)
                {
                    perror("send");
                    close(it->first);
                    FD_CLR(it->first, &master_fds);
                    usernames.erase(it->second.username);
                    it = clients.erase(it);
                }
                else
                    ++it;
            }
        }
    }
}


TcpServer::~TcpServer()
{
    // Close all client connections
    for (int fd = 0; fd <= max_fd; fd++) {
        if (FD_ISSET(fd, &master_fds)) {
            close(fd);
        }
    }
    
    // Close server socket
    close(server_fd);
    
    std::cout << "Server shut down successfully" << std::endl;
}

bool TcpServer::IsDuplicated_Username(const char *new_username)
{
    if (usernames.count(new_username))
    {
        return true;
    }
    return false;   
}

void TcpServer::disconnect_client(int client_fd)
{
    close(client_fd);
    FD_CLR(client_fd, &master_fds);
    usernames.erase(clients[client_fd].username);
    clients.erase(client_fd);
}
