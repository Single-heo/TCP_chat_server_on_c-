#include <iostream>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <array>
#include <sys/select.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>


// Maximum size for network data buffer
#define BUFFER_SIZE 1024
#define DUPLICATED_USERNAME_ERROR "101"
#define MAX_EVENTS 10

// Maximum number of simultaneous client connections allowed
#define MAX_CLIENTS 7


/**
 * TcpServer class - Handles multiple client connections using select()
 * This server can handle multiple clients simultaneously without threading
 * by using I/O multiplexing with the select() system call
 */
class TcpServer{ 
public:
    TcpServer(int _port, const char* ipv4_address = "127.0.0.1");
    
    void initialize_epoll();
    void add_to_epoll(int fd, uint32_t events);
    void remove_from_epoll(int fd);
    void handle_new_connection();
    void Shutting_down();
    void save_credentials(const char* username, const char* password);
    void run();
    ~TcpServer();
    
    int getServerFd() const { return server_fd; }
    int getPort() const { return port; }
    
    struct Client {
        int fd{};
        std::string username{};
        std::string password{};
        std::string read_buffer{};   // Accumulates partial messages
        std::string write_buffer{};  // Data waiting to be sent
        bool registered{};          // Has the client completed username registration?
    };
    
    std::unordered_set<std::string> usernames;
    bool IsDuplicated_Username(const char* username);
    void disconnect_client(int client_fd);
    
    // Map of fd -> Client data
    std::unordered_map<int, Client> clients;

private:
    bool SERVER_IS_RUNNING{true};
    char buffer[BUFFER_SIZE]{};
    char tempUsername[64]{};
    
    int server_fd{};
    int epoll_fd{};
    int port{};
    
    // REMOVE these - we don't need them with epoll:
    // int max_fd{};
    // fd_set read_fds{};
    // fd_set master_fds{};
};