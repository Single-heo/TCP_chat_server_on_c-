#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ÚNICO include do input — usar um só path
#include "../common/input.hpp"

#define BUFFER_SIZE 1024
#define DUPLICATED_USERNAME_ERROR "101"
#define MAX_EVENTS 10
#define MAX_CLIENTS 7

class TcpServer {
public:
    TcpServer(int _port, const char* ipv4_address = "127.0.0.1");
    ~TcpServer();

    void initialize_epoll();
    void add_to_epoll(int fd, uint32_t events);
    void remove_from_epoll(int fd);
    void handle_new_connection();
    void Shutting_down();
    void save_credentials(const std::string& username, const std::string& password);
    void run();

    int getServerFd() const { return server_fd; }
    int getPort() const { return port; }

    struct Client {
        int fd{};
        std::string username{};
        std::string write_buffer{};
    };

    std::unordered_set<std::string> usernames;
    bool IsDuplicated_Username(const char* username);
    void disconnect_client(int client_fd);
    std::unordered_map<int, Client> clients;

private:
    bool SERVER_IS_RUNNING{true};
    char buffer[BUFFER_SIZE]{};
    int server_fd{-1};
    int epoll_fd{-1};
    int port{};
};
