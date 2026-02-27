#pragma once

#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <algorithm>
#include <limits>
#include <stdexcept>

// Inclui input.hpp que já define StringType, bufferEndsWith, etc.
#include "../common/input.hpp"

#define BUFFER_SIZE 1024

enum class Errortype {
    Error_101
};

enum class AuthMode {
    REGISTER,
    LOGIN
};

struct UserCredentials {
    std::string username;
    std::string password;
    bool valid{false};
};

// REMOVIDO: enum StringType { IPV4 = 0 };
// REMOVIDO: declarações de bufferEndsWith, bufferStartsWith, getString
// Tudo já vem de input.hpp

class TcpClient {
private:
    int client_fd{-1};
    std::string client_ip;
    int port{};
    std::string server_ip{};
    sockaddr_in address_of_server{};

    void clearInput() {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

public:
    std::string username{};
    char buffer[BUFFER_SIZE]{};

    UserCredentials get_user_credentials(AuthMode mode);
    std::string format_auth_message(const UserCredentials& creds, AuthMode mode);
    void trim(std::string& str);
    bool validate_credentials(const std::string& username, const std::string& password, AuthMode mode);
    void register_user(int server_socket, Errortype type);
    std::string register_username();
    int getInt(const std::string& prompt, int min = std::numeric_limits<int>::min(), int max = std::numeric_limits<int>::max());

    TcpClient(int _port, const char* client_ip = "127.0.0.1");
    int verify_error_connection(int error_code);
    int connect_and_authenticate(const char* server_ipv4_address = "127.0.0.1");

    const char* getClientIp() const { return client_ip.c_str(); }
    int getClientFd() const { return client_fd; }
    int getPort() const { return port; }

    ~TcpClient() {
        if (client_fd >= 0)
            close(client_fd);
    }
};


TcpClient::TcpClient(int _port, const char* _client_ip)
    : client_ip(_client_ip), port(_port)
{
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        throw std::runtime_error("Socket creation failed");
    }
}

int TcpClient::getInt(const std::string& prompt, int min, int max)
{
    int value;
    while (true) {
        std::cout << prompt;

        if (!(std::cin >> value)) {
            std::cout << "Error: invalid integer input.\n";
            clearInput();
            continue;
        }

        if (value < min || value > max) {
            std::cout << "Error: value must be between "
                      << min << " and " << max << ".\n";
            clearInput();
            continue;
        }

        clearInput();
        return value;
    }
}

int TcpClient::connect_and_authenticate(const char* server_ipv4_address)
{
    server_ip = server_ipv4_address;

    if (inet_addr(server_ipv4_address) == INADDR_NONE) {
        std::cerr << "Invalid IP address: " << server_ipv4_address << std::endl;
        close(client_fd);
        client_fd = -1;
        return -1;
    }

    address_of_server.sin_family = AF_INET;
    address_of_server.sin_addr.s_addr = inet_addr(server_ipv4_address);
    address_of_server.sin_port = htons(port);

    if (connect(client_fd, (struct sockaddr*)&address_of_server, sizeof(address_of_server)) == -1) {
        // FIX: Não fechar client_fd aqui se vai reconectar — recriar o socket
        int err = errno;
        close(client_fd);
        client_fd = socket(AF_INET, SOCK_STREAM, 0); // Recriar para retry
        return verify_error_connection(err);
    }

    std::cout << "1. Register new account\n";
    std::cout << "2. Login with existing account\n";
    int choice = getInt("Choose an option (1 or 2): ", 1, 2);

    AuthMode mode = (choice == 1) ? AuthMode::REGISTER : AuthMode::LOGIN;

    UserCredentials creds;
    int attempts = 0;
    const int MAX_ATTEMPTS = 3;

    while (attempts < MAX_ATTEMPTS) {
        creds = get_user_credentials(mode);

        if (creds.valid)
            break;

        attempts++;
        if (attempts < MAX_ATTEMPTS) {
            std::cout << "\nPlease try again ("
                      << (MAX_ATTEMPTS - attempts)
                      << " attempts remaining)\n\n";
        }
    }

    if (!creds.valid) {
        std::cerr << "Too many failed attempts. Exiting.\n";
        exit(1);
    }

    std::string auth_msg = format_auth_message(creds, mode);
    send(client_fd, auth_msg.c_str(), auth_msg.length(), 0);

    char response[256]{};
    ssize_t n = recv(client_fd, response, sizeof(response) - 1, 0);

    if (n > 0) {
        response[n] = '\0';

        // FIX: strncmp com "Registered" tem 10 chars, não 11
        if (strncmp(response, "Registered", 10) == 0) {
            std::cout << "\n✓ Authentication successful!\n";
            username = creds.username;
        } else if (strncmp(response, "Error:", 6) == 0) {
            // FIX: Tratar mensagem de erro genérica do servidor
            std::cerr << "\n✗ " << response;
            exit(1);
        }
    } else {
        std::cerr << "No response from server\n";
        return -1;
    }

    return 0;
}

int TcpClient::verify_error_connection(int error_code)
{
    switch (error_code) {
        case ECONNREFUSED:
            std::cerr << "Connection refused by server at " << server_ip << ":" << port << std::endl;
            break;
        case ETIMEDOUT:
            std::cerr << "Connection timed out at " << server_ip << ":" << port << std::endl;
            break;
        case EHOSTUNREACH:
            std::cerr << "No route to host " << server_ip << ":" << port << std::endl;
            break;
        case ENETUNREACH:
            std::cerr << "Network unreachable for " << server_ip << ":" << port << std::endl;
            break;
        default:
            std::cerr << "Failed to connect to " << server_ip << ":" << port
                      << " - Error: " << strerror(error_code) << std::endl;
            break;
    }
    return -1;
}

UserCredentials TcpClient::get_user_credentials(AuthMode mode)
{
    UserCredentials creds;
    creds.valid = false;

    if (mode == AuthMode::REGISTER)
        std::cout << "=== User Registration ===\n";
    else
        std::cout << "=== User Login ===\n";

    std::cout << "Enter username: ";
    std::cout.flush();
    std::getline(std::cin, creds.username);

    std::cout << "Enter password: ";
    std::cout.flush();
    std::getline(std::cin, creds.password);

    // FIX: Trim ANTES de validar
    trim(creds.username);
    trim(creds.password);

    if (!validate_credentials(creds.username, creds.password, mode))
        return creds;

    creds.valid = true;
    return creds;
}

bool TcpClient::validate_credentials(const std::string& username,
                                     const std::string& password,
                                     AuthMode mode)
{
    if (username.empty()) {
        std::cerr << "Error: Username cannot be empty\n";
        return false;
    }
    if (username.length() > 50) {
        std::cerr << "Error: Username too long (max 50 chars)\n";
        return false;
    }

    // FIX: Username não deve conter '|' (delimitador do protocolo)
    if (username.find('|') != std::string::npos) {
        std::cerr << "Error: Username cannot contain '|'\n";
        return false;
    }

    if (password.empty()) {
        std::cerr << "Error: Password cannot be empty\n";
        return false;
    }
    if (password.length() > 32) {
        std::cerr << "Error: Password too long (max 32 chars)\n";
        return false;
    }

    if (mode == AuthMode::REGISTER && password.length() < 6) {
        std::cerr << "Error: Password too short (min 6 chars)\n";
        return false;
    }

    return true;
}

void TcpClient::trim(std::string& str)
{
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    str.erase(str.find_last_not_of(" \t\n\r") + 1);
}

std::string TcpClient::format_auth_message(const UserCredentials& creds, AuthMode mode)
{
    if (mode == AuthMode::REGISTER)
        return "/register " + creds.username + "|" + creds.password + "\n";
    else
        return "/login " + creds.username + "|" + creds.password + "\n";
}

void TcpClient::register_user(int server_socket, Errortype type)
{
    if (type == Errortype::Error_101)
        std::cout << "[Error101] This username is already in use\n";

    username = register_username();
    std::string greeting = "/username " + username + "\n";
    send(server_socket, greeting.c_str(), greeting.size(), 0);
}

std::string TcpClient::register_username()
{
    std::string name;
    std::string passwd;

    while (true) {
        std::cout << "Enter your username: ";
        std::cout.flush();
        std::getline(std::cin, name);

        trim(name);

        if (name.empty()) {
            std::cout << "Username cannot be empty.\n";
            continue;
        }
        if (name.length() > 50) {
            std::cout << "Username too long (max 50 chars).\n";
            continue;
        }
        break;
    }

    while (true) {
        std::cout << "Enter your password: ";
        std::cout.flush();
        std::getline(std::cin, passwd);

        trim(passwd);

        if (passwd.empty()) {
            std::cout << "Password cannot be empty.\n";
            continue;
        }
        // FIX: Limite era 8 antes mas validate_credentials usa 32. Unificar.
        if (passwd.length() > 32) {
            std::cout << "Password too long (max 32 chars).\n";
            continue;
        }
        if (passwd.length() < 6) {
            std::cout << "Password too short (min 6 chars).\n";
            continue;
        }
        break;
    }

    return name + "|" + passwd;
}