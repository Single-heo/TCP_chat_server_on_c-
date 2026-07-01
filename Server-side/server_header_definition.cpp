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
// Constructor — builds and arms the listening socket end-to-end.
// Performs, in order: libsodium init, signal setup, socket creation,
// non-blocking + reuse-addr config, bind, and listen.
// Throws std::runtime_error on any unrecoverable failure.
// ============================================================================
TcpServer::TcpServer(int _port, const char* ipv4_address, Logger* _logger)
{
    logger = _logger; // Store non-owning logger pointer (may be null)

    // Initialize libsodium ONCE for the whole process lifetime.
    // Must succeed before any crypto_pwhash_* call is used later.
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialization failed");
    }

    // Ignore SIGPIPE so writing to a closed peer socket won't kill us.
    // Without this, send() to a dead peer raises SIGPIPE and terminates
    // the process by default; we prefer to handle it via the EPIPE errno.
    signal(SIGPIPE, SIG_IGN);

    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;

    // Create an IPv4 TCP stream socket.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::runtime_error(std::string("Socket creation failed: ") + strerror(errno));
    }

    // Non-blocking is mandatory for epoll-driven accept().
    set_NonBlocking(server_fd);

    // SO_REUSEADDR: allows fast rebind after restart (skips TIME_WAIT block).
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        // Non-fatal: server can still run, just may fail to rebind quickly.
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(port); // Host → network byte order

    // inet_pton: validates the address and is IPv6-ready (vs deprecated inet_addr).
    if (inet_pton(AF_INET, ipv4_address, &address.sin_addr) != 1) {
        close(server_fd);
        throw std::runtime_error("Invalid IPv4 address");
    }

    // Bind the socket to the chosen address:port.
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        close(server_fd);
        throw std::runtime_error(std::string("Bind failed: ") + strerror(errno));
    }

    // Start listening; backlog of 100 pending connections.
    if (listen(server_fd, 100) == -1) {
        close(server_fd);
        throw std::runtime_error(std::string("Listen failed: ") + strerror(errno));
    }

    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

// ============================================================================
// Destructor — releases all OS resources in safe order.
// Guarantees no fd leaks even if Shutdown()/run() was never called.
// ============================================================================
TcpServer::~TcpServer()
{
    // Safety net: only flip the flag. If run() executed, clients are already
    // gone; shutdownActiveClients() on an empty map is a harmless no-op.
    SERVER_IS_RUNNING.store(false);
    shutdownActiveClients();

    // Close epoll instance first (stops any further event delivery).
    if (epoll_fd != -1) {
        if (::close(epoll_fd) == 0) epoll_fd = -1;
    }
    // Then close the listening socket itself.
    if (server_fd != -1) {
        if (::close(server_fd) == 0) server_fd = -1;
    }
}


// ============================================================================
// Cryptography (Argon2id via libsodium)
// ============================================================================

// Hashes a plaintext password using Argon2id with "interactive" cost params
// (tuned for low-latency login flows, not for offline attacker resistance
// maximization). The returned string is self-describing (contains salt +
// algorithm + parameters), so no separate salt storage is needed.
std::string TcpServer::hash_password(const std::string& password)
{
    char hash[crypto_pwhash_STRBYTES]; // Output buffer for encoded hash

    // crypto_pwhash_str produces a self-describing hash (algo+salt+params+digest).
    if (crypto_pwhash_str(
            hash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,   // CPU cost
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) { // RAM cost
        // Typically fails only under extreme memory pressure.
        throw std::runtime_error("Password hashing failed (out of memory?)");
    }
    return std::string(hash);
}

// Verifies a plaintext password against a previously stored Argon2id hash.
// Uses libsodium's built-in constant-time comparison to avoid timing attacks.
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

// Sets a file descriptor to non-blocking mode without clobbering other flags.
void TcpServer::set_NonBlocking(int fd)
{
    // Read current flags, then OR in O_NONBLOCK (preserves existing flags).
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) { perror("fcntl F_GETFL"); return; }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
    }
}

// Sends the full buffer, looping until all `length` bytes go out or an
// unrecoverable error occurs. Handles partial writes and EINTR transparently.
// Returns 0 on full success, -1 on failure (peer gone, buffer full, etc.).
int TcpServer::sendAll(int fd, const char* buff, int length)
{
    int total = 0;
    // Loop until every byte is written (send may return fewer bytes than asked).
    while (total < length) {
        ssize_t n = send(fd, buff + total, length - total, 0);
        if (n == -1) {
            if (errno == EINTR) continue;             // Interrupted, retry
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full. For a robust server this should queue the
                // remainder and watch EPOLLOUT. Here we signal failure to caller.
                return -1;
            }
            return -1; // Hard error
        }
        total += static_cast<int>(n);
    }
    return 0;
}

// Convenience overload for std::string payloads — delegates to the raw-buffer version.
int TcpServer::sendAll(int fd, const std::string& data)
{
    return sendAll(fd, data.c_str(), static_cast<int>(data.size()));
}

// ============================================================================
// Epoll Subsystem Control
// ============================================================================

// Creates the epoll instance and registers the listening socket as the
// first monitored fd. Must be called once before run()'s event loop starts.
void TcpServer::initialize_epoll()
{
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
    }

    struct epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET; // Edge-triggered listener (must drain accept queue)
    ev.data.fd = server_fd;

    // Register the listening socket as the first monitored fd.
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        throw std::runtime_error(std::string("epoll_ctl server_fd: ") + strerror(errno));
    }

    std::cout << "Epoll subsystem initialized cleanly\n";
}

// Registers a client fd with epoll under the given event mask
// (caller decides level- vs edge-triggered and IN/OUT interest).
void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
    }
}

// Unregisters an fd from epoll (e.g., right before closing it).
void TcpServer::remove_from_epoll(int fd)
{
    // nullptr event is valid for DEL on modern kernels.
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl: del");
    }
}

// ============================================================================
// Connection Demultiplexing & Identification
// ============================================================================

// O(1) check whether a username is currently online (in the active set).
bool TcpServer::IsDuplicated_Username(const std::string& username)
{
    return usernames.count(username) > 0;
}

// Fully tears down one client: stops epoll monitoring, closes the socket,
// frees its username slot (if authenticated), and removes it from `clients`.
void TcpServer::disconnect_client(int client_fd)
{
    remove_from_epoll(client_fd); // Stop monitoring first
    close(client_fd);             // Release the OS socket

    // Erase from registry and free the username slot if it was authenticated.
    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        if (!it->second.username.empty()) {
            usernames.erase(it->second.username);
        }
        clients.erase(it);
    }
}

// Disconnects every currently active client. Used both during normal
// shutdown and as a destructor safety net.
void TcpServer::shutdownActiveClients()
{
    // 1. Snapshot all active fds first — we can't erase while iterating the map.
    std::vector<int> fds_to_disconnect;
    fds_to_disconnect.reserve(clients.size());

    for (const auto& [fd, client] : clients) {
        fds_to_disconnect.push_back(fd);
    }

    // 2. Safely tear them down one by one.
    for (int fd : fds_to_disconnect) {
        disconnect_client(fd);
    }

    // 3. Clear the unique-username tracking set just in case.
    usernames.clear();
}


// Shutdown — full teardown. Called by the destructor (normal context).
// NEVER call this from a signal handler; use requestShutdown() there
// (which should only set SERVER_IS_RUNNING and let run() exit naturally).
void TcpServer::Shutdown()
{
    SERVER_IS_RUNNING.store(false); // Break the run() loop
    shutdownActiveClients();        // Drop every connection cleanly
}


// handle_new_connection (ET-safe: drain the accept queue)
// Called whenever the listening socket reports EPOLLIN. Because the listener
// is edge-triggered, we MUST accept() in a loop until EAGAIN, otherwise
// pending connections could be silently missed.
void TcpServer::handle_new_connection(Logger* logger)
{
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t   len = sizeof(client_addr);

        int new_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (new_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Queue drained
            if (errno == EINTR) continue;                       // Retry
            perror("accept");
            break;
        }

        // Convert binary peer address to a printable string (thread-safe).
        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::string new_ip = ip_str;

        // Per-IP connection cap (anti-flood): count existing fds from same host.
        int ip_count = 0;
        for (const auto& [fd, client] : clients) {
            if (client.ip_address == new_ip) ip_count++;
        }

        if (ip_count >= MAX_CONNECTIONS_PER_IP) {
            // Reject politely, then close without ever registering the client.
            sendAll(new_fd, "[ERROR]: Connection limit exceeded for this host IP\n");
            close(new_fd);
            continue;
        }

        set_NonBlocking(new_fd);
        add_to_epoll(new_fd, EPOLLIN); // Level-triggered for clients (simpler framing)

        // Build and store the per-client state.
        Client c;
        c.fd         = new_fd;
        c.ip_address = new_ip;
        c.port       = ntohs(client_addr.sin_port); // Network → host byte order
        clients[new_fd] = std::move(c);

        if (logger) {
            logger->Write_log(
                "Fd: " + std::to_string(new_fd) + " New connection from " + new_ip + ":" + std::to_string(clients[new_fd].port),
                Logger::Info);
        }
        std::cout << "New connection from " << new_ip << ":" << std::to_string(clients[new_fd].port) << " (fd: " << new_fd << ")\n";
    }
}


// save_credentials (atomic write via temp file + rename)
// Reads the existing JSON DB (tolerating corruption), appends a new user
// record with a hashed password + UTC timestamp, then writes atomically
// so a crash mid-write never corrupts the live file.
bool TcpServer::save_credentials(const std::string& username,
                                 const std::string& password,
                                 const std::string& ip_addr)
{
    using json = nlohmann::json;
    json data;

    // Load existing DB if present; tolerate corruption by resetting to {}.
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

    // Ensure the "users" array exists before appending.
    if (!data.contains("users") || !data["users"].is_array()) {
        data["users"] = json::array();
    }

    // UTC timestamp for consistency across hosts/timezones.
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_struct{};
    gmtime_r(&t, &tm_struct); // Thread-safe UTC conversion
    std::ostringstream datetime_ss;
    datetime_ss << std::put_time(&tm_struct, "%Y-%m-%dT%H:%M:%SZ"); // ISO-8601 UTC

    // Build the new user record (password is hashed, never stored plain).
    json user;
    user["username"]   = username;
    user["password"]   = hash_password(password);
    user["IP_source"]  = ip_addr;
    user["created_at"] = datetime_ss.str();

    data["users"].push_back(user);

    // Atomic: write to temp, then rename over the original (crash-safe).
    const std::string tmp_path = std::string(CREDENTIALS_PATH) + ".tmp";
    std::ofstream outfile(tmp_path, std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open temp credentials file for writing" << std::endl;
        return false;
    }
    outfile << data.dump(4); // Pretty-print with 4-space indent
    outfile.flush();
    outfile.close();

    // rename() is atomic on POSIX: reader never sees a half-written file.
    if (std::rename(tmp_path.c_str(), CREDENTIALS_PATH) != 0) {
        std::cerr << "Failed to rename temp credentials file: " << strerror(errno) << std::endl;
        std::remove(tmp_path.c_str()); // Clean up the orphan temp on failure
        return false;
    }

    std::cout << "Credentials saved for user: " << username << std::endl;
    return true;
}

// ============================================================================
// username_exists_in_db — on-disk uniqueness check (covers users who
// registered in a previous run and are not currently in the online set).
// ============================================================================
bool TcpServer::username_exists_in_db(const std::string& username)
{
    std::ifstream file(CREDENTIALS_PATH);
    if (!file.is_open()) return false; // No DB yet → not taken

    nlohmann::json data;
    try {
        file >> data;
    } catch (nlohmann::json::parse_error&) {
        return false; // Corrupt DB → treat as not found
    }

    if (!data.contains("users") || !data["users"].is_array()) return false;

    // Linear scan; .value() avoids throwing on missing keys.
    for (const auto& user : data["users"]) {
        if (user.value("username", "") == username) return true;
    }
    return false;
}

// ============================================================================
// verify_credentials — loads the DB and checks the given plaintext password
// against the stored Argon2id hash for a matching username.
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

    // Find the user, then verify the password against its Argon2id hash.
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

// process_message — handles exactly one complete (newline-terminated) record
// already extracted from a client's read buffer. Dispatches to login/register
// handling or, if the client is authenticated, broadcasts it as a chat message.
// Returns false if the client got disconnected (caller must stop using fd);
// currently always returns true because disconnects here are deferred.
bool TcpServer::process_message(int fd, const std::string& raw, Logger& log)
{
    // Copy into the reusable C buffer for the legacy trim/parse utilities.
    memset(buffer, 0, BUFFER_SIZE);
    strncpy(buffer, raw.c_str(), BUFFER_SIZE - 1);
    ssize_t n = static_cast<ssize_t>(strlen(buffer));

    n = trimBuffer(buffer, n);                 // Strip trailing newline/whitespace
    if (isBufferEmpty(buffer, n)) return true; // Nothing to do

    temp_user_credentials temp;

    // parse_credentials returns true for /login and /register commands.
    if (parse_credentials(buffer, temp))
    {
        // ---- REGISTER (cmd_type == 2) ----
        if (temp.cmd_type == 2)
        {
            // Reject if online now OR already persisted on disk.
            if (IsDuplicated_Username(temp.username) ||
                username_exists_in_db(temp.username))
            {
                sendAll(fd, "Error: username already taken\n");
                log.Write_log("Registration failed for " + temp.username +
                              ": username already taken", Logger::Warn);
                return true;
            }

            // Persist before marking online (fail closed if write fails).
            if (!save_credentials(temp.username, temp.password, clients[fd].ip_address)) {
                sendAll(fd, "Error: could not save credentials\n");
                log.Write_log("Persistence failure registering " + temp.username, Logger::Error);
                return true;
            }

            clients[fd].username = temp.username; // Bind session to fd
            usernames.insert(temp.username);      // Mark online

            sendAll(fd, "Registered " + temp.username + "\n");
            log.Write_log("New user registered: " + temp.username, Logger::Info);
            return true;
        }

        // ---- LOGIN (cmd_type == 1) ----
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

        return true; // Parsed but unknown cmd_type
    }

    // ---- Regular chat message ----
    // Require authentication before relaying anything.
    if (clients[fd].username.empty())
    {
        sendAll(fd, "Error: please register or login first\n");
        return true;
    }

    // Broadcast to every OTHER client; collect failed fds for cleanup.
    std::string msg = clients[fd].username + ": " + std::string(buffer) + "\n";
    std::vector<int> to_disconnect;

    for (auto& [client_fd, client_data] : clients)
    {
        if (client_fd == fd) continue; // Don't echo back to sender
        if (sendAll(client_fd, msg) == -1) {
            to_disconnect.push_back(client_fd); // Dead peer — clean up after loop
        }
    }

    // Deferred teardown: avoids mutating the map mid-iteration.
    for (int disc_fd : to_disconnect) {
        log.Write_log("Disconnected client fd=" + std::to_string(disc_fd) +
                      " due to send error", Logger::Warn);
        disconnect_client(disc_fd);
    }

    return true;
}

// ============================================================================
// run (Main Event Loop)
// Loads config, spins up epoll, and processes events until
// SERVER_IS_RUNNING is flipped false (typically by a signal handler).
// ============================================================================
void TcpServer::run()
{
    // Load runtime config from disk; fatal if missing.
    ServerConfig config;
    if (!config.Load("/etc/tcpserver/Config_file.ini")) {
        std::cerr << "Fatal: Failed to load configuration file." << std::endl;
        exit(EXIT_FAILURE);
    }

    Logger log(config);   // Local logger bound to loaded config
    initialize_epoll();   // Arm the epoll instance

    struct epoll_event events[MAX_EVENTS]; // Ready-events output array

    std::cout << "Server running with epoll...\n";

    while (SERVER_IS_RUNNING.load())  // atomic read each iteration
    {
        // Block up to 1000ms; timeout lets us re-check SERVER_IS_RUNNING
        // after a signal flipped the flag (handler does NOT touch fds/maps).
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) continue; // Interrupted by signal — re-check flag
            if (logger) {
                logger->Write_log("Epoll wait error: " + std::string(strerror(errno)), Logger::Error);
            } else {
                std::cerr << "Epoll wait error: " << strerror(errno) << std::endl;
            }
            break; // Unrecoverable epoll error — exit the loop
        }
        if (nfds == 0) continue; // Timeout, no events ready

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;

            // New inbound connection ready on the listening socket.
            if (fd == server_fd) {
                handle_new_connection(logger);
                continue;
            }

            // --- Drain all available data for this client fd ---
            // Level-triggered socket, but we still loop to consume everything
            // currently buffered by the kernel before moving to the next fd.
            bool disconnected = false;
            while (true)
            {
                memset(buffer, 0, BUFFER_SIZE);
                ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

                if (n > 0) {
                    // Append raw bytes to this client's private stream buffer;
                    // framing (splitting on '\n') happens after the drain loop.
                    clients[fd].read_buffer.append(buffer, n);
                    continue;
                }

                if (n == 0) {
                    // Peer performed an orderly shutdown (EOF).
                    log.Write_log("Client disconnected fd=" + std::to_string(fd), Logger::Info);
                    disconnect_client(fd);
                    disconnected = true;
                    break;
                }

                // n < 0: recv error.
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // No more data right now
                if (errno == EINTR) continue;                        // Retry
                perror("recv");
                log.Write_log("Recv error on fd=" + std::to_string(fd) + ": " +
                              strerror(errno), Logger::Error);
                disconnect_client(fd);
                disconnected = true;
                break;
            }

            // Client was closed/disconnected above — skip framing/processing.
            if (disconnected) continue;

            auto it = clients.find(fd);
            if (it == clients.end()) continue; // Safety: fd vanished mid-loop

            // --- Message framing: extract every complete '\n'-terminated
            // record currently sitting in this client's buffer, one at a time.
            size_t newline_pos;
            while ((newline_pos = it->second.read_buffer.find('\n')) != std::string::npos)
            {
                std::string complete = it->second.read_buffer.substr(0, newline_pos + 1);
                it->second.read_buffer.erase(0, newline_pos + 1);

                if (!process_message(fd, complete, log)) {
                    break; // Client got disconnected inside process_message
                }

                // Re-fetch iterator: process_message may have mutated `clients`
                // (e.g., via disconnect_client on a broadcast failure).
                it = clients.find(fd);
                if (it == clients.end()) break;
            }
        }
    }

    // Loop exited (flag flipped by a signal). Teardown runs HERE, in normal
    // context — safe to touch maps, close fds and log. The signal handler
    // itself only set the atomic flag.
    shutdownActiveClients();
    if (logger) {
        logger->Write_log("Event loop exited; active clients shut down.", Logger::Info);
    }
}