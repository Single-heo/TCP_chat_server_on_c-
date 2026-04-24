#include "server-header.hpp"
#include <fstream>  // std::ifstream, std::ofstream for JSON file I/O

// ---------------------------------------------------------------------------
// set_NonBlocking
// ---------------------------------------------------------------------------
void TcpServer::set_NonBlocking(int fd)
{
    // F_GETFL: read the current file status flags for fd
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) { perror("fcntl F_GETFL"); return; }

    // F_SETFL: write back the flags with O_NONBLOCK added.
    // After this, send()/recv()/accept() return EAGAIN immediately
    // instead of sleeping until data/space is available.
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    // Prevent SIGPIPE from killing the process when send() writes
    // to a socket whose remote end has already closed.
    // With SIG_IGN, send() returns -1 and sets errno = EPIPE instead.
    signal(SIGPIPE, SIG_IGN);

    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;

    // AF_INET  = IPv4
    // SOCK_STREAM = TCP (reliable, ordered, byte-stream)
    // 0 = auto-select protocol (TCP for SOCK_STREAM)
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation error" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }

    // Set non-blocking immediately — must be done before epoll registration
    // so that accept() in handle_new_connection() never blocks the event loop
    set_NonBlocking(server_fd);

    // SO_REUSEADDR: lets the OS reuse a port still in TIME_WAIT state.
    // Without this, restarting the server within ~60s of shutdown gives
    // "Address already in use".
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    }

    // Fill the address structure that tells bind() where to listen
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = inet_addr(ipv4_address); // dotted-decimal → 32-bit binary
    address.sin_port        = htons(port);              // host byte order → network (big-endian)

    // Bind: associate the socket with the address/port.
    // Fails if another process owns the port (and SO_REUSEADDR doesn't help).
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_fd);
        throw std::runtime_error("Bind failed");
    }

    // Listen: transition socket to passive mode, ready to accept connections.
    // Backlog of 3 = OS will queue up to 3 unaccepted connections before
    // refusing new ones with ECONNREFUSED.
    if (listen(server_fd, 3) == -1) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        throw std::runtime_error("Listen failed");
    }

    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

// ---------------------------------------------------------------------------
// initialize_epoll
// ---------------------------------------------------------------------------
void TcpServer::initialize_epoll()
{
    // epoll_create1(0): allocates a new epoll instance in the kernel.
    // Returns a file descriptor used in all subsequent epoll_ctl/epoll_wait calls.
    // Flag 0 = no special options (use EPOLL_CLOEXEC to auto-close on exec if needed).
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        throw std::runtime_error("epoll_create1 failed");
    }

    struct epoll_event ev{};
    // EPOLLIN  = notify when data is available to read (new connection on server_fd)
    // EPOLLET  = edge-triggered: notify only once per state change,
    //            not repeatedly while data remains. Requires non-blocking fds.
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd; // store fd in the event so we know which fd fired

    // EPOLL_CTL_ADD: register server_fd with the epoll instance
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        throw std::runtime_error("epoll_ctl failed");
    }

    std::cout << "Epoll initialized successfully\n";
}

// ---------------------------------------------------------------------------
// add_to_epoll / remove_from_epoll
// ---------------------------------------------------------------------------

void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;

    // EPOLL_CTL_ADD: start monitoring `fd` for the specified events
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
    }
}

void TcpServer::remove_from_epoll(int fd)
{
    // EPOLL_CTL_DEL: stop monitoring `fd`.
    // The event pointer can be nullptr on Linux >= 2.6.9.
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl: del");
    }
}

// ---------------------------------------------------------------------------
// hash_password / verify_password
// ---------------------------------------------------------------------------

std::string TcpServer::hash_password(const std::string& password)
{
    // sodium_init() is idempotent but should ideally be called once at startup.
    // Returns 0 on success, 1 if already initialized, -1 on failure.
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium init failed");
    }

    // crypto_pwhash_STRBYTES: size of the output buffer including the
    // encoded salt, algorithm ID, and parameters — all in one string.
    char hash[crypto_pwhash_STRBYTES];

    // crypto_pwhash_str: Argon2id password hashing.
    // OPSLIMIT_INTERACTIVE / MEMLIMIT_INTERACTIVE = tuned for login flows
    // (~65 MB RAM, ~2 iterations) — fast enough for UX, slow enough to resist brute force.
    // Internally generates a random salt; no need to manage it separately.
    if (crypto_pwhash_str(
            hash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("hashing failed (out of memory?)");
    }

    return std::string(hash);
}

bool TcpServer::verify_password(const std::string& password, const std::string& stored_hash)
{
    // crypto_pwhash_str_verify: extracts the salt and parameters embedded
    // in stored_hash, re-hashes `password` with the same settings,
    // and compares in constant time (prevents timing attacks).
    // Returns 0 if the password matches, -1 otherwise.
    return crypto_pwhash_str_verify(
        stored_hash.c_str(),
        password.c_str(),
        password.size()
    ) == 0;
}

// ---------------------------------------------------------------------------
// handle_new_connection
// ---------------------------------------------------------------------------
void TcpServer::handle_new_connection()
{
    sockaddr_in client_addr{};
    socklen_t   len = sizeof(client_addr);

    // accept(): dequeues the first pending connection from the backlog
    // and returns a new fd representing that specific client.
    // client_addr is filled with the client's IP and ephemeral port.
    int new_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
    if (new_fd < 0) {
        // EAGAIN/EWOULDBLOCK = no pending connections right now (normal with EPOLLET)
        perror("accept");
        return;
    }

    // Make the client socket non-blocking so recv()/send() return
    // EAGAIN instead of blocking the event loop
    set_NonBlocking(new_fd);

    // Enforce connection cap — send a human-readable error before closing
    // so the client knows why it was rejected
    if (static_cast<int>(clients.size()) >= MAX_CLIENTS) {
        const char* msg = "Error: server is full\n";
        send(new_fd, msg, strlen(msg), 0);
        close(new_fd);
        return;
    }

    // Register the client fd for read events (level-triggered by default)
    add_to_epoll(new_fd, EPOLLIN);

    // Initialize the client record
    clients[new_fd].fd           = new_fd;
    clients[new_fd].ip_address   = inet_ntoa(client_addr.sin_addr); // binary → dotted-decimal
    clients[new_fd].port         = ntohs(client_addr.sin_port);     // network → host byte order
    clients[new_fd].username     = "";  // empty until /register or /login succeeds
    clients[new_fd].write_buffer = "";

    std::cout << "Client connected fd=" << new_fd
              << ", IP=" << clients[new_fd].ip_address
              << ", Port=" << clients[new_fd].port << std::endl;
}

// ---------------------------------------------------------------------------
// Shutting_down
// ---------------------------------------------------------------------------
void TcpServer::Shutting_down()
{
    // The run() while loop checks this flag at the top of every iteration.
    // Setting it false causes a clean exit after the current batch of events.
    SERVER_IS_RUNNING = false;
}

// ---------------------------------------------------------------------------
// save_credentials
// ---------------------------------------------------------------------------
void TcpServer::save_credentials(const std::string& username,
                                  const std::string& password,
                                  const std::string& ip_addr)
{
    using json = nlohmann::json;
    json data;

    // Try to load existing credentials — if the file doesn't exist yet,
    // we start with a fresh structure below
    std::ifstream infile("../DataBase/credentials.json");
    if (infile.is_open()) {
        try {
            infile >> data;
        } catch (json::parse_error& e) {
            // Corrupt or empty file — reset rather than propagating bad data
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            data["users"] = json::array();
        }
        infile.close();
    } else {
        data["users"] = json::array();
    }

    // Defensive check: ensure the top-level structure is always valid
    // even if the file had unexpected content
    if (!data.contains("users") || !data["users"].is_array()) {
        data["users"] = json::array();
    }

    // Build a timestamp for the created_at field
    auto now    = std::chrono::system_clock::now();
    std::time_t t      = std::chrono::system_clock::to_time_t(now);
    std::tm*    tm_ptr = std::localtime(&t);

    std::ostringstream datetime_ss;
    datetime_ss << std::put_time(tm_ptr, "%Y-%m-%d %H:%M:%S");

    // Build the new user entry — password is hashed before storage
    json user;
    user["username"]   = username;
    user["password"]   = hash_password(password); // Argon2id hash, never plaintext
    user["IP_source"]  = ip_addr;
    user["created_at"] = datetime_ss.str();

    data["users"].push_back(user);

    // Overwrite the file with the updated array (pretty-printed, indent=4)
    std::ofstream outfile("../DataBase/credentials.json");
    if (outfile.is_open()) {
        outfile << data.dump(4);
        outfile.close();
        std::cout << "Credentials saved for user: " << username << std::endl;
    } else {
        std::cerr << "Failed to open credentials file for writing" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// verify_credentials
// ---------------------------------------------------------------------------
bool TcpServer::verify_credentials(const std::string& username,
                                    const std::string& password)
{
    std::ifstream file("../DataBase/credentials.json");
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

    if (!data.contains("users") || !data["users"].is_array())
        return false;

    for (const auto& user : data["users"]) {
        const std::string stored_username = user["username"];
        const std::string stored_hash     = user["password"];

        // verify_password re-hashes the input with the salt embedded
        // in stored_hash and compares in constant time
        if (stored_username == username && verify_password(password, stored_hash))
            return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// sendAll
// ---------------------------------------------------------------------------
int TcpServer::sendAll(int fd, const char* buff, int length)
{
    int total = 0;

    // send() is not guaranteed to send all bytes in one call —
    // especially on non-blocking sockets. Loop until everything is sent.
    while (total < length) {
        // buff + total: advance the pointer past already-sent bytes
        // length - total: only request the remaining bytes
        int n = send(fd, buff + total, length - total, 0);
        if (n == -1) return -1; // EAGAIN or real error — caller decides what to do
        total += n;
    }

    return 0; // all bytes delivered to the kernel send buffer
}

// ---------------------------------------------------------------------------
// run — main event loop
// ---------------------------------------------------------------------------
void TcpServer::run()
{
    initialize_epoll();

    // Stack-allocated array of event structs — filled by epoll_wait each iteration
    struct epoll_event events[MAX_EVENTS];

    std::cout << "Server running with epoll...\n";

    while (SERVER_IS_RUNNING)
    {
        // epoll_wait: block until at least one fd is ready, or 1000ms timeout.
        // The timeout ensures the loop re-checks SERVER_IS_RUNNING periodically
        // even when there's no network activity.
        // Returns: number of ready fds, 0 on timeout, -1 on error.
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) continue; // signal interrupted the wait — harmless, retry
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        if (nfds == 0) continue; // timeout — no events, loop back

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd; // which fd triggered this event

            // ── New incoming connection ──────────────────────────────────
            if (fd == server_fd) {
                handle_new_connection();
                continue;
            }

            // ── Existing client sent data ────────────────────────────────
            memset(buffer, 0, BUFFER_SIZE); // clear stale bytes from previous iteration

            // recv(): read up to BUFFER_SIZE-1 bytes, leaving room for '\0'
            // On non-blocking fd, returns EAGAIN if no data is ready
            ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

            if (n <= 0) {
                // n == 0: client sent FIN (graceful close)
                // n <  0: recv error (ECONNRESET, etc.)
                if (n == 0)
                    std::cout << "Client disconnected fd=" << fd << std::endl;
                else
                    perror("recv");

                disconnect_client(fd);
                continue;
            }

            // Check for a newline before trimming, because trim may remove it.
            // A newline signals that this recv() completed a full message.
            bool has_newline = bufferEndsWith(buffer, n, "\n");

            // Remove leading/trailing whitespace; returns adjusted length
            n = trimBuffer(buffer, n);

            // Ignore empty or whitespace-only messages
            if (isBufferEmpty(buffer, n)) continue;

            // ── Authentication commands ──────────────────────────────────
            // parse_credentials detects "/register user|pass" or "/login user|pass"
            temp_user_credentials temp;
            if (parse_credentials(buffer, temp))
            {
                if (temp.cmd_type == 2) { // /register
                    if (IsDuplicated_Username(temp.username.c_str())) {
                        const char* error_msg = "Error: username already taken\n";
                        send(fd, error_msg, strlen(error_msg), 0);
                        continue;
                    }
                    // Hash and persist the new user; acknowledge success
                    save_credentials(temp.username, temp.password, clients[fd].ip_address);
                    std::string success_msg = "Registered " + temp.username + "\n";
                    send(fd, success_msg.c_str(), success_msg.size(), 0);
                }

                if (temp.cmd_type == 1) { // /login
                    if (verify_credentials(temp.username, temp.password)) {
                        std::string success_msg = "Login successful for " + temp.username + "\n";
                        send(fd, success_msg.c_str(), success_msg.size(), 0);
                    } else {
                        const char* error_msg = "Error: invalid username or password\n";
                        send(fd, error_msg, strlen(error_msg), 0);
                        continue;
                    }
                }

                // Bind the username to this connection for the session
                clients[fd].username = temp.username;
                clients[fd].write_buffer.clear();
                usernames.insert(temp.username); // reserve the username slot
                continue;
            }

            // ── Guard: reject chat from unauthenticated clients ──────────
            if (clients[fd].username.empty()) {
                const char* error_msg = "Error: please register first\n";
                send(fd, error_msg, strlen(error_msg), 0);
                continue;
            }

            // ── Message reassembly ───────────────────────────────────────
            // TCP is a stream protocol — a single logical message may arrive
            // across multiple recv() calls. Accumulate chunks until '\n'.
            clients[fd].write_buffer += buffer;

            if (!has_newline) continue; // message incomplete, wait for more data

            // Complete message received — build the broadcast string
            std::string msg = clients[fd].username + ": " +
                              clients[fd].write_buffer + "\n";

            clients[fd].write_buffer.clear();

            // ── Broadcast to all other clients ───────────────────────────
            // Collect fds that fail instead of disconnecting mid-iteration
            // to avoid invalidating the clients map iterator
            std::vector<int> to_disconnect;

            for (auto& [client_fd, client_data] : clients)
            {
                if (client_fd == fd) continue; // don't echo back to sender

                ssize_t sent = sendAll(client_fd, msg.c_str(), msg.size());
                if (sent == -1) {
                    perror("sendAll");
                    to_disconnect.push_back(client_fd);
                }
            }

            // Safe to disconnect now that iteration is finished
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
    // Close all active client sockets first
    for (auto& [fd, client] : clients) {
        close(fd);
    }
    clients.clear();

    // Close the epoll instance and the listening socket
    if (epoll_fd >= 0) close(epoll_fd);
    if (server_fd >= 0) close(server_fd);

    std::cout << "Server shut down successfully" << std::endl;
}

// ---------------------------------------------------------------------------
// IsDuplicated_Username
// ---------------------------------------------------------------------------
bool TcpServer::IsDuplicated_Username(const char* new_username)
{
    // unordered_set::count returns 1 if found, 0 if not — O(1) average
    return usernames.count(new_username) > 0;
}

// ---------------------------------------------------------------------------
// disconnect_client
// ---------------------------------------------------------------------------
void TcpServer::disconnect_client(int client_fd)
{
    remove_from_epoll(client_fd); // epoll stops watching this fd
    close(client_fd);             // OS releases the file descriptor

    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        usernames.erase(it->second.username); // free the username so others can take it
        clients.erase(it);                    // remove from the active client registry
    }
}