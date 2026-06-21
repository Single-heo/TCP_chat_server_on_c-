#include "server-header.hpp"
#include <fstream>
#include <vector>
#include <cstring>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <stdexcept>

// ============================================================================
// Constructor
// ============================================================================
TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    // Initialize libsodium ONCE for the whole process lifetime.
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialization failed");
    }

    // Ignore SIGPIPE so writing to a closed peer socket won't kill us.
    signal(SIGPIPE, SIG_IGN);

    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::runtime_error(std::string("Socket creation failed: ") + strerror(errno));
    }

    set_NonBlocking(server_fd);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(port);

    // inet_pton: validates the address and is IPv6-ready (vs deprecated inet_addr).
    if (inet_pton(AF_INET, ipv4_address, &address.sin_addr) != 1) {
        close(server_fd);
        throw std::runtime_error("Invalid IPv4 address");
    }

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        close(server_fd);
        throw std::runtime_error(std::string("Bind failed: ") + strerror(errno));
    }

    if (listen(server_fd, 100) == -1) {
        close(server_fd);
        throw std::runtime_error(std::string("Listen failed: ") + strerror(errno));
    }

    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

// ============================================================================
// Destructor
// ============================================================================
TcpServer::~TcpServer()
{
    for (auto& [fd, client] : clients) {
        close(fd);
    }
    clients.clear();
    usernames.clear();

    if (epoll_fd  >= 0) close(epoll_fd);
    if (server_fd >= 0) close(server_fd);

    std::cout << "Server shut down successfully" << std::endl;
}

// ============================================================================
// Cryptography (Argon2id via libsodium)
// ============================================================================
std::string TcpServer::hash_password(const std::string& password)
{
    char hash[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str(
            hash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("Password hashing failed (out of memory?)");
    }
    return std::string(hash);
}

bool TcpServer::verify_password(const std::string& password, const std::string& stored_hash)
{
    return crypto_pwhash_str_verify(
        stored_hash.c_str(),
        password.c_str(),
        password.size()) == 0;
}

// ============================================================================
// I/O Socket Control Utilities
// ============================================================================
void TcpServer::set_NonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) { perror("fcntl F_GETFL"); return; }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
    }
}

int TcpServer::sendAll(int fd, const char* buff, int length)
{
    int total = 0;
    while (total < length) {
        ssize_t n = send(fd, buff + total, length - total, 0);
        if (n == -1) {
            if (errno == EINTR) continue;             // interrupted, retry
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full. For a robust server this should queue the
                // remainder and watch EPOLLOUT. Here we signal failure to caller.
                return -1;
            }
            return -1;
        }
        total += static_cast<int>(n);
    }
    return 0;
}

int TcpServer::sendAll(int fd, const std::string& data)
{
    return sendAll(fd, data.c_str(), static_cast<int>(data.size()));
}

// ============================================================================
// Epoll Subsystem Control
// ============================================================================
void TcpServer::initialize_epoll()
{
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
    }

    struct epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET; // Edge-triggered listener
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        throw std::runtime_error(std::string("epoll_ctl server_fd: ") + strerror(errno));
    }

    std::cout << "Epoll subsystem initialized cleanly\n";
}

void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
    }
}

void TcpServer::remove_from_epoll(int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl: del");
    }
}

// ============================================================================
// Connection Demultiplexing & Identification
// ============================================================================
bool TcpServer::IsDuplicated_Username(const std::string& username)
{
    return usernames.count(username) > 0;
}

void TcpServer::disconnect_client(int client_fd)
{
    remove_from_epoll(client_fd);
    close(client_fd);

    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        if (!it->second.username.empty()) {
            usernames.erase(it->second.username);
        }
        clients.erase(it);
    }
}

void TcpServer::Shutting_down()
{
    SERVER_IS_RUNNING = false;
}

// ============================================================================
// handle_new_connection (ET-safe: drain the accept queue)
// ============================================================================
void TcpServer::handle_new_connection(Logger& log)
{
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t   len = sizeof(client_addr);

        int new_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (new_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // drained
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str)); // thread-safe
        std::string new_ip = ip_str;

        // Per-IP connection cap (anti-flood)
        int ip_count = 0;
        for (const auto& [fd, client] : clients) {
            if (client.ip_address == new_ip) ip_count++;
        }

        if (ip_count >= MAX_CONNECTIONS_PER_IP) {
            sendAll(new_fd, "[ERROR]: Connection limit exceeded for this host IP\n");
            close(new_fd);
            continue;
        }

        set_NonBlocking(new_fd);
        add_to_epoll(new_fd, EPOLLIN); // level-triggered for clients

        Client c;
        c.fd         = new_fd;
        c.ip_address = new_ip;
        c.port       = ntohs(client_addr.sin_port);
        clients[new_fd] = std::move(c);

        log.Write_log(
            "Fd: " + std::to_string(new_fd) + " New connection from " + new_ip + ":" + std::to_string(clients[new_fd].port),
            Logger::Info);
    }
}

// ============================================================================
// save_credentials (atomic write via temp file + rename)
// ============================================================================
bool TcpServer::save_credentials(const std::string& username,
                                 const std::string& password,
                                 const std::string& ip_addr)
{
    using json = nlohmann::json;
    json data;

    std::ifstream infile(CREDENTIALS_PATH);
    if (infile.is_open()) {
        try {
            infile >> data;
        } catch (json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            data = json::object();
        }
        infile.close();
    }

    if (!data.contains("users") || !data["users"].is_array()) {
        data["users"] = json::array();
    }

    // UTC timestamp for consistency across hosts/timezones.
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_struct{};
    gmtime_r(&t, &tm_struct);
    std::ostringstream datetime_ss;
    datetime_ss << std::put_time(&tm_struct, "%Y-%m-%dT%H:%M:%SZ");

    json user;
    user["username"]   = username;
    user["password"]   = hash_password(password);
    user["IP_source"]  = ip_addr;
    user["created_at"] = datetime_ss.str();

    data["users"].push_back(user);

    // Atomic: write to temp, then rename over the original.
    const std::string tmp_path = std::string(CREDENTIALS_PATH) + ".tmp";
    std::ofstream outfile(tmp_path, std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open temp credentials file for writing" << std::endl;
        return false;
    }
    outfile << data.dump(4);
    outfile.flush();
    outfile.close();

    if (std::rename(tmp_path.c_str(), CREDENTIALS_PATH) != 0) {
        std::cerr << "Failed to rename temp credentials file: " << strerror(errno) << std::endl;
        std::remove(tmp_path.c_str());
        return false;
    }

    std::cout << "Credentials saved for user: " << username << std::endl;
    return true;
}

// ============================================================================
// username_exists_in_db
// ============================================================================
bool TcpServer::username_exists_in_db(const std::string& username)
{
    std::ifstream file(CREDENTIALS_PATH);
    if (!file.is_open()) return false;

    nlohmann::json data;
    try {
        file >> data;
    } catch (nlohmann::json::parse_error&) {
        return false;
    }

    if (!data.contains("users") || !data["users"].is_array()) return false;

    for (const auto& user : data["users"]) {
        if (user.value("username", "") == username) return true;
    }
    return false;
}

// ============================================================================
// verify_credentials (safe JSON access)
// ============================================================================
bool TcpServer::verify_credentials(const std::string& username,
                                   const std::string& password)
{
    std::ifstream file(CREDENTIALS_PATH);
    if (!file.is_open()) {
        std::cerr << "Failed to open credentials file\n";
        return false;
    }

    nlohmann::json data;
    try {
        file >> data;
    } catch (nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
        return false;
    }

    if (!data.contains("users") || !data["users"].is_array()) return false;

    for (const auto& user : data["users"]) {
        const std::string stored_username = user.value("username", "");
        const std::string stored_hash     = user.value("password", "");
        if (stored_username == username &&
            !stored_hash.empty() &&
            verify_password(password, stored_hash)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// process_message — handles exactly one complete (newline-terminated) record.
// Returns false if the client got disconnected (caller must stop using fd).
// ============================================================================
bool TcpServer::process_message(int fd, const std::string& raw, Logger& log)
{
    // Copy into the reusable C buffer for the legacy trim/parse utilities.
    memset(buffer, 0, BUFFER_SIZE);
    strncpy(buffer, raw.c_str(), BUFFER_SIZE - 1);
    ssize_t n = static_cast<ssize_t>(strlen(buffer));

    n = trimBuffer(buffer, n);
    if (isBufferEmpty(buffer, n)) return true; // nothing to do

    temp_user_credentials temp;

    if (parse_credentials(buffer, temp))
    {
        // ---- REGISTER ----
        if (temp.cmd_type == 2)
        {
            // Online-session duplicate OR already persisted
            if (IsDuplicated_Username(temp.username) ||
                username_exists_in_db(temp.username))
            {
                sendAll(fd, "Error: username already taken\n");
                log.Write_log("Registration failed for " + temp.username +
                              ": username already taken", Logger::Warn);
                return true;
            }

            if (!save_credentials(temp.username, temp.password, clients[fd].ip_address)) {
                sendAll(fd, "Error: could not save credentials\n");
                log.Write_log("Persistence failure registering " + temp.username, Logger::Error);
                return true;
            }

            clients[fd].username = temp.username;
            usernames.insert(temp.username);

            sendAll(fd, "Registered " + temp.username + "\n");
            log.Write_log("New user registered: " + temp.username, Logger::Info);
            return true;
        }

        // ---- LOGIN ----
        if (temp.cmd_type == 1)
        {
            // Prevent the same account being online twice.
            if (IsDuplicated_Username(temp.username)) {
                sendAll(fd, "Error: user already logged in\n");
                log.Write_log("Duplicate login blocked for " + temp.username, Logger::Warn);
                return true;
            }

            if (verify_credentials(temp.username, temp.password)) {
                clients[fd].username = temp.username;
                usernames.insert(temp.username);
                sendAll(fd, "Login successful for " + temp.username + "\n");
                log.Write_log("User logged in: " + temp.username, Logger::Info);
            } else {
                sendAll(fd, "Error: invalid username or password\n");
            }
            return true;
        }

        return true; // parsed but unknown cmd_type
    }

    // ---- Regular chat message ----
    if (clients[fd].username.empty())
    {
        sendAll(fd, "Error: please register or login first\n");
        return true;
    }

    std::string msg = clients[fd].username + ": " + std::string(buffer) + "\n";
    std::vector<int> to_disconnect;

    for (auto& [client_fd, client_data] : clients)
    {
        if (client_fd == fd) continue;
        if (sendAll(client_fd, msg) == -1) {
            to_disconnect.push_back(client_fd);
        }
    }

    for (int disc_fd : to_disconnect) {
        log.Write_log("Disconnected client fd=" + std::to_string(disc_fd) +
                      " due to send error", Logger::Warn);
        disconnect_client(disc_fd);
    }

    return true;
}

// ============================================================================
// run (Main Event Loop)
// ============================================================================
void TcpServer::run()
{
    ServerConfig config;
    if (!config.Load("/etc/tcpserver/Config_file.ini")) {
        std::cerr << "Fatal: Failed to load configuration file." << std::endl;
        exit(EXIT_FAILURE);
    }

    Logger log(config);
    initialize_epoll();

    struct epoll_event events[MAX_EVENTS];
    log.Write_log("Starting server", Logger::Info);
    std::cout << "Server running with epoll...\n";

    while (SERVER_IS_RUNNING)
    {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) continue;
            log.Write_log("Epoll wait error: " + std::string(strerror(errno)), Logger::Error);
            break;
        }
        if (nfds == 0) continue;

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                handle_new_connection(log);
                continue;
            }

            // Drain everything readable, then frame messages by '\n'.
            bool disconnected = false;
            while (true)
            {
                memset(buffer, 0, BUFFER_SIZE);
                ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

                if (n > 0) {
                    clients[fd].read_buffer.append(buffer, n);
                    // With level-triggered + this single read we may still have
                    // more data; recv loop continues until EAGAIN.
                    continue;
                }

                if (n == 0) {
                    log.Write_log("Client disconnected fd=" + std::to_string(fd), Logger::Info);
                    disconnect_client(fd);
                    disconnected = true;
                    break;
                }

                // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // no more data
                if (errno == EINTR) continue;
                perror("recv");
                log.Write_log("Recv error on fd=" + std::to_string(fd) + ": " +
                              strerror(errno), Logger::Error);
                disconnect_client(fd);
                disconnected = true;
                break;
            }

            if (disconnected) continue;

            // Process ALL complete messages currently in the buffer.
            auto it = clients.find(fd);
            if (it == clients.end()) continue; // safety

            size_t newline_pos;
            while ((newline_pos = it->second.read_buffer.find('\n')) != std::string::npos)
            {
                std::string complete = it->second.read_buffer.substr(0, newline_pos + 1);
                it->second.read_buffer.erase(0, newline_pos + 1);

                if (!process_message(fd, complete, log)) {
                    break; // client was disconnected mid-processing
                }

                // Re-validate: process_message may have removed this fd.
                it = clients.find(fd);
                if (it == clients.end()) break;
            }
        }
    }
}
