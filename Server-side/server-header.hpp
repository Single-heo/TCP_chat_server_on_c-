#pragma once

// Password hashing (Argon2 algorithm) — used via libsodium's crypto_pwhash API
#include <argon2.h>
// libsodium: crypto primitives for hashing and future encryption
#include <sodium.h>
// fcntl: F_GETFL, F_SETFL for setting O_NONBLOCK on sockets
#include <fcntl.h>
#include <iostream>
// epoll_create1, epoll_ctl, epoll_wait — Linux I/O event notification
#include <sys/epoll.h>
// sockaddr_in: IPv4 socket address structure
#include <netinet/in.h>
// socket(), bind(), listen(), accept(), send(), recv()
#include <sys/socket.h>
// POSIX types: ssize_t, socklen_t, etc.
#include <sys/types.h>
// getaddrinfo — not used directly but kept for future DNS resolution support
#include <netdb.h>
// close(), read()
#include <unistd.h>
// inet_addr(), inet_ntoa(), htons() — IP/port byte-order conversions
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
#include <vector>
// nlohmann/json: header-only JSON library for credential persistence
#include <nlohmann/json.hpp>

// Shared utilities: bufferEndsWith, trimBuffer, isBufferEmpty,
// parse_credentials, getString, getInt, etc.
#include "../common/input.hpp"

#define BUFFER_SIZE 1024               // Max bytes consumed per recv() call
#define DUPLICATED_USERNAME_ERROR "101" // Protocol error code: username already taken
#define MAX_EVENTS 10                  // Max events returned per epoll_wait() call


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
    // Loops on send() to handle partial writes.
    // Returns 0 on success, -1 on error.
    int sendAll(int fd, const char* buff, int length);

    // Sets O_NONBLOCK on `fd` via fcntl so all I/O calls return
    // immediately with EAGAIN instead of blocking the event loop.
    void set_NonBlocking(int fd);

    // Creates the epoll instance and registers server_fd for
    // EPOLLIN|EPOLLET (new incoming connections, edge-triggered).
    void initialize_epoll();

    // Registers `fd` with epoll under the given event mask
    // (e.g. EPOLLIN, EPOLLIN|EPOLLET, EPOLLIN|EPOLLOUT).
    void add_to_epoll(int fd, uint32_t events);

    // Deregisters `fd` from epoll monitoring.
    // Safe to call with nullptr event (Linux >= 2.6.9).
    void remove_from_epoll(int fd);

    // Accepts a pending connection on server_fd.
    // Rejects with an error message if MAX_CLIENTS is reached.
    // Sets the new fd non-blocking and registers it with epoll.
    void handle_new_connection();

    // Sets SERVER_IS_RUNNING = false, causing run() to exit
    // after the current epoll_wait iteration completes.
    void Shutting_down();

    // Reads the credentials JSON file, appends a new user entry
    // with a hashed password and timestamp, and writes it back.
    void save_credentials(const std::string& username,
                          const std::string& password,
                          const std::string& ip_addr);

    // Opens the credentials JSON file and searches for a matching
    // username + password (via verify_password). Returns true if found.
    bool verify_credentials(const std::string& username,
                            const std::string& password);

    // Main event loop. Calls initialize_epoll() then blocks on
    // epoll_wait(), dispatching to handle_new_connection() or
    // the message handling path on each ready fd.
    void run();

    int getServerFd() const { return server_fd; }
    int getPort()     const { return port; }

    // Per-client state stored in the clients map
    struct Client {
        int fd{};                  // Socket file descriptor
        std::string ip_address{};  // Client's dotted-decimal IPv4 address
        int port{};                // Client's ephemeral source port
        std::string username{};    // Set after /register or /login; empty until then
        std::string write_buffer{};// Accumulates recv() chunks until '\n' is seen
    };

    // All currently authenticated and in-progress usernames.
    // Checked before registration to enforce uniqueness within a session.
    std::unordered_set<std::string> usernames;

    // Returns true if `username` is already in the active usernames set.
    bool IsDuplicated_Username(const char* username);

    // Full client teardown: removes from epoll, closes fd,
    // frees the username slot, and erases from the clients map.
    void disconnect_client(int client_fd);

    // Primary client registry: maps socket fd → Client struct.
    // All active connections (authenticated or not) live here.
    std::unordered_map<int, Client> clients;

private:
    bool SERVER_IS_RUNNING{true}; // Loop control; set false by Shutting_down()
    char buffer[BUFFER_SIZE]{};   // Reusable recv buffer, zeroed before each read
    int server_fd{-1};            // The listening socket fd
    int epoll_fd{-1};             // The epoll instance fd
    int port{};                   // Port this server is bound to
};