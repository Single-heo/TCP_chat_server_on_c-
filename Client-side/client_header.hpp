#pragma once

#include <iostream>
#include <netinet/in.h>  // sockaddr_in
#include <sys/socket.h>  // socket, connect, send, recv
#include <unistd.h>      // close
#include <arpa/inet.h>   // inet_addr, htons
#include <cstring>       // memset, strerror, strlen
#include <string>
#include <algorithm>
#include <limits>        // std::numeric_limits (used in getInt and clearInput)
#include <stdexcept>     // std::runtime_error
#include <openssl/ssl.h> // SSL_CTX, SSL, SSL_connect, SSL_read, SSL_write
#include <openssl/err.h> // ERR_print_errors_fp
#include "../common/input.hpp"

#define BUFFER_SIZE 1024

// Protocol-level error codes returned by the server
enum class Errortype {
    Error_101  // Duplicate username
};

// Controls which authentication flow to use when connecting
enum class AuthMode {
    REGISTER,
    LOGIN
};

// Holds a parsed username/password pair and a validity flag
struct UserCredentials {
    std::string username;
    std::string password;
    bool valid{false};  // true only if validate_credentials() passed
};

class TcpClient {
private:
    int client_fd{-1};           // Socket file descriptor (-1 = not created/closed)
    std::string client_ip;       // Local IP the client is bound from (informational)
    int port{};                  // Target server port
    std::string server_ip{};     // Target server IP (stored for error messages)
    sockaddr_in address_of_server{}; // Resolved server address struct for connect()

    // ── TLS state ──────────────────────────────────────────────────────────
    SSL_CTX* ssl_ctx{nullptr};   // Client TLS context (created once per connection attempt)
    SSL* ssl{nullptr};           // Active TLS session, once handshake succeeds

    // Flushes cin state after invalid input to avoid infinite error loops
    void clearInput() {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    // Builds ssl_ctx with the right verification mode.
    // ca_cert_path empty  -> SSL_VERIFY_NONE (dev/self-signed only, prints a warning)
    // ca_cert_path set    -> loads it and requires SSL_VERIFY_PEER against it
    bool init_ssl_ctx(const std::string& ca_cert_path);

public:
    std::string username{};      // Authenticated username (set after successful auth)
    char buffer[BUFFER_SIZE]{};  // Reusable recv buffer

    /*
    - This function do the verifications on the commands send by the client in other words everything with "/" at the buffer
    - If the command is not recognized it will send an error message to the client and ignore the command
    - If the command is recognized it will execute the corresponding action (for now we only have the /username command to set the username of the client)
    @param command the command send by the client
    @param sockfd the socket file descriptor
    */
    void verify_command(std::string *input_buffer, int sockfd);

    // Prompts for username and password; validates and returns a UserCredentials struct
    UserCredentials get_user_credentials(AuthMode mode);

    // Formats credentials into the wire protocol string:
    // "/register username|password\n" or "/login username|password\n"
    std::string format_auth_message(const UserCredentials& creds, AuthMode mode);

    // In-place trim of leading/trailing whitespace from a string
    void trim(std::string& str);

    // Validates username/password against length, content, and mode-specific rules
    bool validate_credentials(const std::string& username,
                              const std::string& password,
                              AuthMode mode);

    // Legacy handler: re-prompts for username when server returns a duplicate error
    void register_user(int server_socket, Errortype type);

    // Legacy interactive username+password prompt (used by register_user)
    std::string register_username();

    // Safe integer input with optional min/max bounds
    int getInt(const std::string& prompt,
               int min = std::numeric_limits<int>::min(),
               int max = std::numeric_limits<int>::max());

    // Constructor: creates the TCP socket; throws on failure
    TcpClient(int _port, const char* client_ip = "127.0.0.1");

    // Translates errno codes from connect() into human-readable error messages
    int verify_error_connection(int error_code);

    // Performs the TLS client handshake over the already-connected client_fd.
    // Must be called after connect() succeeds and before any application data
    // is exchanged. Returns true on success.
    bool tls_handshake(const std::string& ca_cert_path = "");

    // Send/receive helpers that go through the active TLS session.
    // Mirror send()/recv() semantics: return the number of bytes transferred,
    // 0 on clean peer shutdown, -1 on error.
    ssize_t tls_send(const void* buf, size_t len);
    ssize_t tls_recv(void* buf, size_t len);

    // Connects to the server, performs the TLS handshake, then runs the full
    // register/login handshake over TLS. Returns 0 on success, -1 on failure.
    // ca_cert_path: PEM file to verify the server's certificate against.
    //               Leave empty only for local dev with a self-signed cert.
    int connect_and_authenticate(const char* server_ipv4_address = "127.0.0.1",
                                  const std::string& ca_cert_path = "");

    const char* getClientIp() const { return client_ip.c_str(); }
    int getClientFd()         const { return client_fd; }
    int getPort()             const { return port; }

    // Closes the TLS session/context and the socket if still open
    ~TcpClient() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ssl_ctx) {
            SSL_CTX_free(ssl_ctx);
        }
        if (client_fd >= 0)
            close(client_fd);
    }
};
