#include "client_header.hpp"
// REMOVIDO: #include "../common/input.hpp"  — já incluído via client_header.hpp
#include <algorithm>
#include <unistd.h>
#include <sys/select.h>
#include <cerrno>
#include <fcntl.h>
#include <termios.h>

#define DUPLICATED_USERNAME_ERROR "101"

// Globais para estado do terminal
static termios old_term;
static int original_stdin_flags;
static std::string input_buffer{};
static std::string username{};

// Declarações
void restore_stdin();
void setup_stdin();
void handle_stdin(int sockfd);

int main()
{
    int port = 25565;
    std::string server_ip = getString("Enter server ip address: ", false, true, StringType::IPV4);
    TcpClient client(port, "127.0.0.1");

    while (true) {
        int conn_result = client.connect_and_authenticate(server_ip.c_str());
        if (conn_result == 0)
            break;
        std::cout << "Retrying connection in 5 seconds...\n";
        sleep(5);
    }

    


    int sockfd = client.getClientFd();

    // Salvar terminal ANTES de modificar
    tcgetattr(STDIN_FILENO, &old_term);
    setup_stdin();

    fd_set read_fds;
    int max_fd = std::max(STDIN_FILENO, sockfd);

    // FIX: connect_and_authenticate() já consumiu o "Registered" do servidor.
    // Se chegou aqui, autenticação foi bem-sucedida.
    bool registered = true;
    username = client.username;

    std::cout << username << "> ";
    std::cout.flush();

    std::string socket_buffer;

    while (true)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);

        if (select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (registered && FD_ISSET(STDIN_FILENO, &read_fds))
            handle_stdin(sockfd);

        if (FD_ISSET(sockfd, &read_fds))
        {
            char buf[1024]{};
            ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);

            if (n <= 0) {
                std::cout << "\nServer disconnected.\n";
                restore_stdin();
                close(sockfd);
                return 0;
            }

            buf[n] = '\0';
            socket_buffer.append(buf, n);

            size_t pos;
            while ((pos = socket_buffer.find('\n')) != std::string::npos)
            {
                std::string line = socket_buffer.substr(0, pos + 1);
                socket_buffer.erase(0, pos + 1);

                // Mensagens de outros clientes
                std::cout << "\r\033[K" << line;
                std::cout << username << "> " << input_buffer;
                std::cout.flush();
            }
        }
    }

    restore_stdin();
    close(sockfd);
    return 0;
}

void restore_stdin()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags);
}

void setup_stdin()
{
    original_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags | O_NONBLOCK);

    termios new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
}

void handle_stdin(int sockfd)
{
    char c;

    while (true)
    {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0)
            break;

        if (c == '\n' || c == '\r')
        {
            if (!input_buffer.empty())
            {
                if (input_buffer == "/clear") {
                    system("clear");
                    input_buffer.clear();
                    std::cout << username << "> ";
                    std::cout.flush();
                    continue;
                }
                if (input_buffer == "/exit") {
                    restore_stdin();
                    std::cout << "\nExiting chat client.\n";
                    close(sockfd);
                    exit(0);
                }

                input_buffer += '\n';
                send(sockfd, input_buffer.c_str(), input_buffer.size(), 0);
                input_buffer.clear();
            }

            std::cout << "\n" << username << "> ";
            std::cout.flush();
            continue;
        }

        if (c == 127 || c == '\b')
        {
            if (!input_buffer.empty()) {
                input_buffer.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
            continue;
        }

        // FIX: Só aceitar caracteres imprimíveis
        if (c >= 32 && c < 127) {
            input_buffer += c;
            std::cout << c;
            std::cout.flush();
        }
    }
}
