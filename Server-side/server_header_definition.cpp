#include "server-header.hpp"
#include <fstream>  // std::ifstream, std::ofstream for JSON file I/O

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    // Ignore SIGPIPE so that send() to a closed socket returns -1 instead of
    // crashing the process with a signal
    signal(SIGPIPE, SIG_IGN);

    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;

    // AF_INET = IPv4 | SOCK_STREAM = TCP | protocol 0 = auto-select
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation error" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }

    // SO_REUSEADDR: allows re-binding to a port in TIME_WAIT state immediately
    // after server restart — avoids "Address already in use" errors
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    }

    // Configure the server address structure
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = inet_addr(ipv4_address); // convert dotted-decimal to binary
    address.sin_port        = htons(port);              // host byte order -> network byte order

    // Bind the socket to the address/port
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_fd);
        throw std::runtime_error("Bind failed");
    }

    // Start listening; backlog of 3 means up to 3 connections can queue
    // before accept() is called
    if (listen(server_fd, 3) == -1) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        throw std::runtime_error("Listen failed");
    }

    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

// ---------------------------------------------------------------------------
// Epoll setup
// ---------------------------------------------------------------------------
void TcpServer::initialize_epoll()
{
    // epoll_create1(0): creates epoll instance; flag 0 = default (no CLOEXEC here)
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        throw std::runtime_error("epoll_create1 failed");
    }

    // Register the server socket for EPOLLIN (incoming connection events)
    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        throw std::runtime_error("epoll_ctl failed");
    }

    std::cout << "Epoll initialized successfully\n";
}

// ---------------------------------------------------------------------------
// Epoll helpers
// ---------------------------------------------------------------------------

// Registers fd with the given event mask (e.g. EPOLLIN, EPOLLIN|EPOLLOUT)
void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
    }
}

// Removes fd from epoll; passing nullptr as event is valid since Linux 2.6.9
void TcpServer::remove_from_epoll(int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl: del");
    }
}

// ---------------------------------------------------------------------------
// New connection handler
// ---------------------------------------------------------------------------
void TcpServer::handle_new_connection()
{
    sockaddr_in client_addr{};
    socklen_t   len = sizeof(client_addr);

    // accept() extracts the first queued connection and returns a new fd for it
    int new_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
    if (new_fd < 0) {
        perror("accept");
        return;
    }

    // Enforce the MAX_CLIENTS cap: send an error and close immediately
    // so the connecting client gets a clear rejection message
    if (static_cast<int>(clients.size()) >= MAX_CLIENTS) {
        const char* msg = "Error: server is full\n";
        send(new_fd, msg, strlen(msg), 0);
        close(new_fd);
        return;
    }

    // Register the new fd for read events and add to the client map
    add_to_epoll(new_fd, EPOLLIN);

    clients[new_fd].fd           = new_fd;
    clients[new_fd].username     = "";   // username is empty until /register or /login
    clients[new_fd].write_buffer = "";

    std::cout << "Client connected fd=" << new_fd << std::endl;
}

// ---------------------------------------------------------------------------
// Graceful shutdown trigger
// ---------------------------------------------------------------------------
void TcpServer::Shutting_down()
{
    // Setting this flag causes the run() while-loop to exit on the next iteration
    SERVER_IS_RUNNING = false;
}

// ---------------------------------------------------------------------------
// Credential persistence
// ---------------------------------------------------------------------------
void TcpServer::save_credentials(const std::string& username, const std::string& password)
{
    using json = nlohmann::json;
    json data;

    // Attempt to read the existing credentials file
    std::ifstream infile("../DataBase/credentials.json");

    if (infile.is_open()) {
        try {
            infile >> data;  // deserialize JSON from file
        } catch (json::parse_error& e) {
            // File is corrupt or empty — start fresh
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            data["users"] = json::array();
        }
        infile.close();
    } else {
        // File doesn't exist yet — initialize structure
        data["users"] = json::array();
    }

    // Safety check: ensure "users" key exists and is an array,
    // even if the JSON had unexpected structure
    if (!data.contains("users") || !data["users"].is_array()) {
        data["users"] = json::array();
    }
    // Get current time
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = std::localtime(&t);


    // --- Option 2: Full timestamp "2025-02-26 14:35:22" ---
    std::ostringstream datetime_ss;
    datetime_ss << std::put_time(tm_ptr, "%Y-%m-%d %H:%M:%S");
    std::string created_at = datetime_ss.str();  // "2025-03-05 14:35:22"

    // Build the new user entry
    json user;
    user["username"]   = username;
    user["password"]   = password;             // TODO: hash before storing (e.g. bcrypt)
    user["created_at"] = created_at;         // TODO: replace with real timestamp via <chrono>

    data["users"].push_back(user);

    // Write back the updated JSON (pretty-printed with indent=4)
    std::ofstream outfile("../DataBase/credentials.json");
    if (outfile.is_open()) {
        outfile << data.dump(4);
        outfile.close();
        std::cout << "Credentials saved for user: " << username << std::endl;
    } else {
        std::cerr << "Failed to open credentials file for writing" << std::endl;
    }
}

bool TcpServer::verify_credentials(const std::string &username, const std::string &password)
{
    // Open and parse the JSON file
    std::ifstream file("../DataBase/credentials.json");
    nlohmann::json data;
    file >> data;

    // Loop through all users
    for (const auto& user : data["users"]) {
        std::string created_at = user["created_at"];
        std::string password   = user["password"];
        std::string username   = user["username"];

        // Check for matching username and password
        if (username == username && password == password) {
            return true; // Valid credentials
        }
        return false; // No match found
    }
}

// ---------------------------------------------------------------------------
// Main event loop
// ---------------------------------------------------------------------------
void TcpServer::run()
{
    initialize_epoll();

    struct epoll_event events[MAX_EVENTS]; // buffer for events returned by epoll_wait

    std::cout << "Server running with epoll...\n";

    while (SERVER_IS_RUNNING)
    {
        // Block for up to 1000 ms waiting for any registered fd to become ready.
        // Timeout allows the loop to re-check SERVER_IS_RUNNING periodically.
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) continue; // interrupted by signal — just retry
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        if (nfds == 0) continue; // timeout with no events — loop back

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;

            // ── New incoming connection ──────────────────────────────────────
            if (fd == server_fd) {
                handle_new_connection();
                continue;
            }

            // ── Data from an existing client ────────────────────────────────
            memset(buffer, 0, BUFFER_SIZE); // clear stale data before reading

            // Read up to BUFFER_SIZE-1 bytes (leave room for null terminator)
            ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

            if (n <= 0) {
                // n == 0: client closed connection gracefully
                // n <  0: socket error
                if (n == 0)
                    std::cout << "Client disconnected fd=" << fd << std::endl;
                else
                    perror("recv");

                disconnect_client(fd);
                continue;
            }

            // Detect whether the received data ends with a newline
            // (used to decide if the message is complete)
            bool has_newline = bufferEndsWith(buffer, n, "\n");

            // Strip leading/trailing whitespace from the raw buffer
            n = trimBuffer(buffer, n);

            // Ignore empty or whitespace-only messages
            if (isBufferEmpty(buffer, n)) continue;

            // ── Authentication commands (/register or /login) ────────────────
            temp_user_credentials temp;
            if (parse_credentials(buffer, temp))
            {
                if (temp.cmd_type == 2) {
                    // Registration: check for duplicate username first
                    if (IsDuplicated_Username(temp.username.c_str())) {
                        const char* error_msg = "Error: username already taken\n";
                        send(fd, error_msg, strlen(error_msg), 0);
                        continue;
                    }
                    // Persist credentials and acknowledge success
                    save_credentials(temp.username, temp.password);
                    std::string success_msg = "Registered " + temp.username + "\n";
                    send(fd, success_msg.c_str(), success_msg.size(), 0);
                }
                // TODO: cmd_type == 1 (login) — validate credentials against JSON
                if(temp.cmd_type == 1) {
                    // For now, just acknowledge the login attempt without validation
                    if(verify_credentials(temp.username, temp.password)){
                            std::string success_msg = "Login successful for " + temp.username + "\n";
                            send(fd, success_msg.c_str(), success_msg.size(), 0);
                        } else {
                            const char* error_msg = "Error: invalid username or password\n";
                            send(fd, error_msg, strlen(error_msg), 0);
                            continue;
                    }
                }

                // Associate the username with this fd
                clients[fd].username = temp.username;
                clients[fd].write_buffer.clear();
                usernames.insert(temp.username); // mark username as taken
                continue;
            }

            // ── Reject unauthenticated chat messages ─────────────────────────
            // A client must register/login before sending chat messages
            if (clients[fd].username.empty()) {
                const char* error_msg = "Error: please register first\n";
                send(fd, error_msg, strlen(error_msg), 0);
                continue;
            }

            // ── Accumulate message chunks ────────────────────────────────────
            // Messages may arrive in multiple recv() calls; buffer until '\n'
            clients[fd].write_buffer += buffer;

            if (!has_newline) continue; // message is incomplete — wait for more data

            // Build the final broadcast message: "username: message\n"
            std::string msg = clients[fd].username + ": " +
                              clients[fd].write_buffer + "\n";

            clients[fd].write_buffer.clear(); // reset for next message

            // ── Broadcast to all OTHER clients ───────────────────────────────
            // Collect failing fds first to avoid iterator invalidation
            // while erasing from the clients map mid-loop
            std::vector<int> to_disconnect;

            for (auto& [client_fd, client_data] : clients)
            {
                if (client_fd == fd) continue; // skip sender

                ssize_t sent = send(client_fd, msg.c_str(), msg.size(), 0);
                if (sent <= 0) {
                    perror("send");
                    to_disconnect.push_back(client_fd); // mark for removal
                }
            }

            // Disconnect failed clients after the iteration is complete
            for (int disc_fd : to_disconnect) {
                disconnect_client(disc_fd);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
TcpServer::~TcpServer()
{
    // Close all client sockets
    for (auto& [fd, client] : clients) {
        close(fd);
    }
    clients.clear();

    // Close control file descriptors
    if (epoll_fd >= 0) close(epoll_fd);
    if (server_fd >= 0) close(server_fd);

    std::cout << "Server shut down successfully" << std::endl;
}

// ---------------------------------------------------------------------------
// Username uniqueness check
// ---------------------------------------------------------------------------
bool TcpServer::IsDuplicated_Username(const char* new_username)
{
    // unordered_set::count is O(1) average — returns 1 if found, 0 otherwise
    return usernames.count(new_username) > 0;
}

// ---------------------------------------------------------------------------
// Client disconnection
// ---------------------------------------------------------------------------
void TcpServer::disconnect_client(int client_fd)
{
    remove_from_epoll(client_fd); // stop monitoring this fd
    close(client_fd);             // release the OS file descriptor

    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        usernames.erase(it->second.username); // free the username slot
        clients.erase(it);                    // remove from active client map
    }
}
