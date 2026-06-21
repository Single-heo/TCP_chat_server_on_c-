#pragma once

// libsodium: crypto primitives (Argon2id hashing + future encryption)
#include <sodium.h>
// The logging system
#include "common/Logger/logger.hpp"
// The configuration header that loads the config_file.ini
#include "common/config/Configuration.hpp"
// fcntl: F_GETFL, F_SETFL for setting O_NONBLOCK on sockets
#include <fcntl.h>
// epoll_create1, epoll_ctl, epoll_wait — Linux I/O event notification
#include <sys/epoll.h>
// sockaddr_in: IPv4 socket address structure
#include <netinet/in.h>
// socket(), bind(), listen(), accept(), send(), recv()
#include <sys/socket.h>
// POSIX types: ssize_t, socklen_t, etc.
#include <sys/types.h>
// getaddrinfo — kept for future DNS resolution support
#include <netdb.h>
// close(), read()
#include <unistd.h>
// inet_pton(), inet_ntop(), htons() — IP/port byte-order conversions (thread-safe)
#include <arpa/inet.h>
// memset(), strerror(), strlen()
#include <cstring>
// signal(), SIGPIPE, SIG_IGN
#include <csignal>
// errno — error codes from system calls
#include <cerrno>
// fd → Client map (O(1) average lookup by socket fd)
#include <unordered_map>
// Active username set for O(1) duplicate detection
#include <unordered_set>
#include <string>
// nlohmann/json: header-only JSON library for credential persistence
#include <nlohmann/json.hpp>

// Shared utilities: bufferEndsWith, trimBuffer, isBufferEmpty,
// parse_credentials, getString, getInt, etc.
#include <input.hpp>

#define BUFFER_SIZE 1024                // Max bytes consumed per recv() call
#define DUPLICATED_USERNAME_ERROR "101" // Protocol error code: username already taken
#define MAX_EVENTS 10                   // Max events returned per epoll_wait() call
#define MAX_CONNECTIONS_PER_IP 5        // Anti connection-flood per host
#define CREDENTIALS_PATH "/var/lib/tcpserver/credentials.json"


class TcpServer {
public:
    // Creates the listening socket, sets SO_REUSEADDR, binds to
    // ipv4_address:_port, and calls listen(). Throws on failure.
    TcpServer(int _port = 25565, const char* ipv4_address = "127.0.0.1");

    // Closes all client fds, the epoll fd, and the server fd
    ~TcpServer();

    // Verifies a plaintext password against an Argon2id hash from the DB.
    // Uses crypto_pwhash_str_verify — never compares plaintext directly.
    bool verify_password(const std::string& password, const std::string& stored_hash);

    // Hashes a plaintext password with Argon2id (interactive cost parameters).
    // Returns the encoded hash string (includes salt, algorithm, params).
    std::string hash_password(const std::string& password);

    // Guarantees all `length` bytes of `buff` are sent to `fd`.
    // Loops on send() to handle partial writes. Returns 0 on success, -1 on error.
    int sendAll(int fd, const char* buff, int length);

    // Convenience wrapper to send a std::string fully.
    int sendAll(int fd, const std::string& data);

    // Sets O_NONBLOCK on `fd` via fcntl.
    void set_NonBlocking(int fd);

    // Creates the epoll instance and registers server_fd (EPOLLIN|EPOLLET).
    void initialize_epoll();

    // Registers `fd` with epoll under the given event mask.
    void add_to_epoll(int fd, uint32_t events);

    // Deregisters `fd` from epoll monitoring.
    void remove_from_epoll(int fd);

    // Accepts pending connections on server_fd (ET-safe loop).
    void handle_new_connection(Logger& log);

    // Sets SERVER_IS_RUNNING = false, causing run() to exit.
    void Shutting_down();

    // Appends a new user with hashed password + timestamp. Atomic write.
    // Returns true on success.
    bool save_credentials(const std::string& username,
                          const std::string& password,
                          const std::string& ip_addr);

    // Returns true if username already exists in the persistent store.
    bool username_exists_in_db(const std::string& username);

    // Searches for matching username + password. Returns true if found.
    bool verify_credentials(const std::string& username,
                            const std::string& password);

    // Main event loop.
    void run();

    int getServerFd() const { return server_fd; }
    int getPort()     const { return port; }

    // Per-client state stored in the clients map
    struct Client {
        int fd{};                  // Socket file descriptor
        std::string ip_address{};  // Client's dotted-decimal IPv4 address
        int port{};                // Client's ephemeral source port
        std::string username{};    // Set after /register or /login; empty until then
        std::string read_buffer{}; // Accumulates recv() chunks until '\n' is seen
    };

    // All currently authenticated usernames (online session uniqueness).
    std::unordered_set<std::string> usernames;

    // Returns true if `username` is currently online.
    bool IsDuplicated_Username(const std::string& username);

    // Full client teardown: removes from epoll, closes fd, frees username, erases.
    void disconnect_client(int client_fd);

    // Primary client registry: maps socket fd → Client struct.
    std::unordered_map<int, Client> clients;

private:
    // Processes one complete (newline-terminated) message for a given fd.
    // Returns false if the client was disconnected during processing.
    bool process_message(int fd, const std::string& raw, Logger& log);

    bool SERVER_IS_RUNNING{true}; // Loop control; set false by Shutting_down()
    char buffer[BUFFER_SIZE]{};   // Reusable recv buffer
    int server_fd{-1};            // The listening socket fd
    int epoll_fd{-1};             // The epoll instance fd
    int port{};                   // Port this server is bound to
};
