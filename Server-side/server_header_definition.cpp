#include "server-header.hpp"
#include <fstream>

TcpServer::TcpServer(int _port, const char* ipv4_address)
{
    signal(SIGPIPE, SIG_IGN);

    std::cout << "Starting TCP server on " << ipv4_address << ":" << _port << "...\n";
    port = _port;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation error" << std::endl;
        throw std::runtime_error("Socket creation failed");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ipv4_address);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_fd);
        throw std::runtime_error("Bind failed");
    }

    if (listen(server_fd, 3) == -1) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        throw std::runtime_error("Listen failed");
    }

    std::cout << "Server is listening on " << ipv4_address << ":" << port << std::endl;
}

void TcpServer::initialize_epoll()
{
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        throw std::runtime_error("epoll_create1 failed");
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        throw std::runtime_error("epoll_ctl failed");
    }

    std::cout << "Epoll initialized successfully\n";
}

void TcpServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev{};
    ev.events = events;
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

void TcpServer::handle_new_connection()
{
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);

    int new_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
    if (new_fd < 0) {
        perror("accept");
        return;
    }

    // FIX: Rejeitar conexão se limite de clientes foi atingido
    if (static_cast<int>(clients.size()) >= MAX_CLIENTS) {
        const char* msg = "Error: server is full\n";
        send(new_fd, msg, strlen(msg), 0);
        close(new_fd);
        return;
    }

    add_to_epoll(new_fd, EPOLLIN);

    clients[new_fd].fd = new_fd;
    clients[new_fd].username = "";
    clients[new_fd].write_buffer = "";

    std::cout << "Client connected fd=" << new_fd << std::endl;
}

void TcpServer::Shutting_down()
{
    SERVER_IS_RUNNING = false;
}

void TcpServer::save_credentials(const std::string& username, const std::string& password)
{
    using json = nlohmann::json;
    json data;
    std::ifstream infile("../DataBase/credentials.json");

    if (infile.is_open()) {
        try {
            infile >> data;
        } catch (json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            data["users"] = json::array();
        }
        infile.close();
    } else {
        data["users"] = json::array();
    }

    // FIX: Garantir que "users" existe e é array
    if (!data.contains("users") || !data["users"].is_array()) {
        data["users"] = json::array();
    }

    json user;
    user["username"] = username;
    user["password"] = password;
    user["created_at"] = "2025-02-26"; // TODO: usar timestamp real

    data["users"].push_back(user);

    std::ofstream outfile("../DataBase/credentials.json");
    if (outfile.is_open()) {
        outfile << data.dump(4);
        outfile.close();
        std::cout << "Credentials saved for user: " << username << std::endl;
    } else {
        std::cerr << "Failed to open credentials file for writing" << std::endl;
    }
}

void TcpServer::run()
{
    initialize_epoll();

    struct epoll_event events[MAX_EVENTS];

    std::cout << "Server running with epoll...\n";

    while (SERVER_IS_RUNNING)
    {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        if (nfds == 0)
            continue;

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                handle_new_connection();
                continue;
            }

            memset(buffer, 0, BUFFER_SIZE);

            ssize_t n = recv(fd, buffer, BUFFER_SIZE - 1, 0);

            if (n <= 0) {
                if (n == 0)
                    std::cout << "Client disconnected fd=" << fd << std::endl;
                else
                    perror("recv");

                disconnect_client(fd);
                continue;
            }

            bool has_newline = bufferEndsWith(buffer, n, "\n");

            n = trimBuffer(buffer, n);

            if (isBufferEmpty(buffer, n))
                continue;

            temp_user_credentials temp;
            if (parse_credentials(buffer, temp))
            {
                if (temp.cmd_type == 2) {
                    if (IsDuplicated_Username(temp.username.c_str())) {
                        const char* error_msg = "Error: username already taken\n";
                        send(fd, error_msg, strlen(error_msg), 0);
                        continue;
                    }
                    save_credentials(temp.username, temp.password);
                    std::string success_msg = "Registered " + temp.username + "\n";
                    send(fd, success_msg.c_str(), success_msg.size(), 0);
                }
                // FIX: Para login (cmd_type == 1), também precisa validar credenciais
                // TODO: implementar verificação de login no JSON

                clients[fd].username = temp.username;
                clients[fd].write_buffer.clear();
                usernames.insert(temp.username);

                continue;
            }

            // FIX: Rejeitar mensagens de clientes sem username registrado
            if (clients[fd].username.empty()) {
                const char* error_msg = "Error: please register first\n";
                send(fd, error_msg, strlen(error_msg), 0);
                continue;
            }

            clients[fd].write_buffer += buffer;

            if (!has_newline)
                continue;

            std::string msg = clients[fd].username + ": " +
                              clients[fd].write_buffer + "\n";

            clients[fd].write_buffer.clear();

            // FIX: Iterator invalidation — coletar fds a desconectar e processar depois
            std::vector<int> to_disconnect;

            for (auto& [client_fd, client_data] : clients)
            {
                if (client_fd == fd)
                    continue;

                ssize_t sent = send(client_fd, msg.c_str(), msg.size(), 0);

                if (sent <= 0) {
                    perror("send");
                    to_disconnect.push_back(client_fd);
                }
            }

            // Desconectar fora do loop de iteração
            for (int disc_fd : to_disconnect) {
                disconnect_client(disc_fd);
            }
        }
    }
}

TcpServer::~TcpServer()
{
    for (auto& [fd, client] : clients) {
        close(fd);
    }
    clients.clear();

    if (epoll_fd >= 0)
        close(epoll_fd);
    if (server_fd >= 0)
        close(server_fd);

    std::cout << "Server shut down successfully" << std::endl;
}

bool TcpServer::IsDuplicated_Username(const char* new_username)
{
    return usernames.count(new_username) > 0;
}

void TcpServer::disconnect_client(int client_fd)
{
    remove_from_epoll(client_fd);
    close(client_fd);

    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        usernames.erase(it->second.username);
        clients.erase(it);
    }
}
