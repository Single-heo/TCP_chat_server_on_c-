#pragma once

#include <iostream>
#include <sys/epoll.h>      // epoll_create1, epoll_ctl, epoll_wait
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket, bind, listen, accept, send, recv
#include <sys/types.h>      // POSIX types
#include <netdb.h>          // getaddrinfo (not used directly, kept for compatibility)
#include <unistd.h>         // close, read
#include <arpa/inet.h>      // inet_addr, htons
#include <cstring>          // memset, strerror, strlen
#include <csignal>          // signal, SIGPIPE, SIG_IGN
#include <cerrno>           // errno
#include <unordered_map>    // client registry
#include <unordered_set>    // username uniqueness tracking
#include <string>
#include <vector>
#include <nlohmann/json.hpp> // JSON serialization for credentials storage

// Shared input utilities: bufferEndsWith, trimBuffer, isBufferEmpty, parse_credentials, etc.
#include "../common/input.hpp"

#define BUFFER_SIZE 1024        // Max bytes per recv() call
#define DUPLICATED_USERNAME_ERROR "101"  // Protocol error code for duplicate usernames
#define MAX_EVENTS 10           // Max epoll events processed per iteration
#define MAX_CLIENTS 7           // Hard limit on simultaneous connected clients

class TcpServer {
public:
    // Constructs the server: creates socket, sets SO_REUSEADDR, binds, and starts listening
    TcpServer(int _port, const char* ipv4_address = "127.0.0.1");

    // Destructor: closes all client fds, epoll fd, and server fd
    ~TcpServer();

    // Creates epoll instance and registers the server fd for incoming connections
    void initialize_epoll();

    // Registers a file descriptor with epoll under the specified event mask
    void add_to_epoll(int fd, uint32_t events);

    // Removes a file descriptor from epoll monitoring
    void remove_from_epoll(int fd);

    // Accepts a new incoming connection; rejects if MAX_CLIENTS is reached
    void handle_new_connection();

    // Signals the run() loop to stop gracefully
    void Shutting_down();

    // Persists a username/password pair into the JSON credentials database
    void save_credentials(const std::string& username, const std::string& password);

    // Verify if the user already has a login on the system
    bool verify_credentials(const std::string& username, const std::string& password);

    // Main event loop: uses epoll to multiplex server fd and all client fds
    void run();

    int getServerFd() const { return server_fd; }
    int getPort()     const { return port; }

    // Represents a connected client with its socket fd, display name, and incomplete message buffer
    struct Client {
        int fd{};
        std::string username{};
        std::string write_buffer{};  // Accumulates partial messages until '\n' is received
    };

    // Set of active usernames — used for fast O(1) duplicate detection
    std::unordered_set<std::string> usernames;

    // Returns true if the given username is already registered in this session
    bool IsDuplicated_Username(const char* username);

    // Cleanly removes a client: deregisters from epoll, closes fd, erases from maps
    void disconnect_client(int client_fd);

    // Maps socket fd -> Client struct for all currently connected clients
    std::unordered_map<int, Client> clients;

private:
    bool SERVER_IS_RUNNING{true};   // Loop control flag; set to false by Shutting_down()
    char buffer[BUFFER_SIZE]{};     // Reusable recv buffer (zeroed before each read)
    int server_fd{-1};              // Listening socket fd
    int epoll_fd{-1};               // Epoll instance fd
    int port{};                     // Port the server is bound to
};
