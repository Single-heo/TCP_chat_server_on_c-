#include "client_header.hpp"
// input.hpp is already included transitively via client_header.hpp
#include <algorithm>
#include <unistd.h>      // read, close, sleep
#include <sys/select.h>  // select, FD_SET, FD_ZERO, FD_ISSET
#include <cerrno>        // errno, EINTR
#include <fcntl.h>       // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <termios.h>     // tcgetattr, tcsetattr, termios — raw terminal control

#define DUPLICATED_USERNAME_ERROR "101"

// ── Module-level state ───────────────────────────────────────────────────────
static termios old_term;            // Original terminal settings (restored on exit)
static int original_stdin_flags;    // Original stdin fcntl flags (for O_NONBLOCK cleanup)
static std::string input_buffer{};  // Accumulates characters typed by the user
static std::string username{};      // Display name prepended to the prompt

// Forward declarations
void restore_stdin();
void setup_stdin();
void handle_stdin(int sockfd);

// ---------------------------------------------------------------------------
// Constructor — creates the TCP socket
// ---------------------------------------------------------------------------
TcpClient::TcpClient(int _port, const char* _client_ip)
    : client_ip(_client_ip), port(_port)
{
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        throw std::runtime_error("Socket creation failed");
    }
}

// ---------------------------------------------------------------------------
// Integer input helper with bounds checking
// ---------------------------------------------------------------------------
int TcpClient::getInt(const std::string& prompt, int min, int max)
{
    int value;
    while (true) {
        std::cout << prompt;

        if (!(std::cin >> value)) {
            std::cout << "Error: invalid integer input.\n";
            clearInput(); // flush bad state before retrying
            continue;
        }

        if (value < min || value > max) {
            std::cout << "Error: value must be between "
                      << min << " and " << max << ".\n";
            clearInput();
            continue;
        }

        clearInput(); // consume trailing newline left by cin >>
        return value;
    }
}

// ---------------------------------------------------------------------------
// connect_and_authenticate
// Establishes the TCP connection and performs the register/login handshake.
// Returns 0 on success, -1 on failure.
// ---------------------------------------------------------------------------
int TcpClient::connect_and_authenticate(const char* server_ipv4_address)
{
    server_ip = server_ipv4_address;

    // Validate the IP string before passing it to connect()
    if (inet_addr(server_ipv4_address) == INADDR_NONE) {
        std::cerr << "Invalid IP address: " << server_ipv4_address << std::endl;
        close(client_fd);
        client_fd = -1;
        return -1;
    }

    // Fill in the server address structure
    address_of_server.sin_family      = AF_INET;
    address_of_server.sin_addr.s_addr = inet_addr(server_ipv4_address);
    address_of_server.sin_port        = htons(port);

    if (connect(client_fd,
                (struct sockaddr*)&address_of_server,
                sizeof(address_of_server)) == -1)
    {
        // On failure: capture errno, recreate socket for a potential retry,
        // then return the translated error
        int err = errno;
        close(client_fd);
        client_fd = socket(AF_INET, SOCK_STREAM, 0); // fresh socket for retry
        return verify_error_connection(err);
    }

    // ── Auth mode selection ──────────────────────────────────────────────────
    std::cout << "1. Register new account\n";
    std::cout << "2. Login with existing account\n";
    int choice = getInt("Choose an option (1 or 2): ", 1, 2);

    AuthMode mode = (choice == 1) ? AuthMode::REGISTER : AuthMode::LOGIN;

    // ── Credential collection with retry ────────────────────────────────────
    UserCredentials creds;
    int attempts = 0;
    const int MAX_ATTEMPTS = 3;

    while (attempts < MAX_ATTEMPTS) {
        creds = get_user_credentials(mode);

        if (creds.valid) break; // stop as soon as we have valid input

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

    // ── Send credentials to server ───────────────────────────────────────────
    std::string auth_msg = format_auth_message(creds, mode);
    send(client_fd, auth_msg.c_str(), auth_msg.length(), 0);

    // ── Read server response ─────────────────────────────────────────────────
    char response[256]{};
    ssize_t n = recv(client_fd, response, sizeof(response) - 1, 0);

    if (n > 0) {
        response[n] = '\0';

        // "Registered" is 10 characters — check prefix to handle trailing newline
        if (strncmp(response, "Registered", 10) == 0) {
            std::cout << "\n✓ Authentication successful!\n";
            username = creds.username; // store for use in the chat loop
        } else if (strncmp(response, "Error:", 6) == 0) {
            std::cerr << "\n✗ " << response;
            exit(1);
        }
    } else {
        std::cerr << "No response from server\n";
        return -1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// verify_error_connection — maps errno values to descriptive messages
// ---------------------------------------------------------------------------
int TcpClient::verify_error_connection(int error_code)
{
    switch (error_code) {
        case ECONNREFUSED:
            std::cerr << "Connection refused by server at "
                      << server_ip << ":" << port << std::endl;
            break;
        case ETIMEDOUT:
            std::cerr << "Connection timed out at "
                      << server_ip << ":" << port << std::endl;
            break;
        case EHOSTUNREACH:
            std::cerr << "No route to host "
                      << server_ip << ":" << port << std::endl;
            break;
        case ENETUNREACH:
            std::cerr << "Network unreachable for "
                      << server_ip << ":" << port << std::endl;
            break;
        default:
            std::cerr << "Failed to connect to " << server_ip << ":" << port
                      << " - Error: " << strerror(error_code) << std::endl;
            break;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// get_user_credentials — interactive prompt for username + password
// ---------------------------------------------------------------------------
UserCredentials TcpClient::get_user_credentials(AuthMode mode)
{
    UserCredentials creds;
    creds.valid = false;

    // Context-sensitive header
    if (mode == AuthMode::REGISTER)
        std::cout << "=== User Registration ===\n";
    else
        std::cout << "=== User Login ===\n";

    std::cout << "Enter username: ";
    std::cout.flush();
    std::getline(std::cin, creds.username); // getline preserves spaces in names

    std::cout << "Enter password: ";
    std::cout.flush();
    std::getline(std::cin, creds.password);

    // Trim BEFORE validation so rules apply to actual content, not padding
    trim(creds.username);
    trim(creds.password);

    if (!validate_credentials(creds.username, creds.password, mode))
        return creds; // valid remains false

    creds.valid = true;
    return creds;
}

// ---------------------------------------------------------------------------
// validate_credentials — enforces protocol and security constraints
// ---------------------------------------------------------------------------
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

    // '|' is the field delimiter in the wire protocol ("/register user|pass\n")
    // — allowing it would break parsing on the server side
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

    // Minimum length only enforced on registration (login must accept old passwords)
    if (mode == AuthMode::REGISTER && password.length() < 6) {
        std::cerr << "Error: Password too short (min 6 chars)\n";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// trim — removes leading and trailing whitespace in-place
// ---------------------------------------------------------------------------
void TcpClient::trim(std::string& str)
{
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    str.erase(str.find_last_not_of(" \t\n\r") + 1);
}

// ---------------------------------------------------------------------------
// format_auth_message — builds the wire-protocol authentication string
// ---------------------------------------------------------------------------
std::string TcpClient::format_auth_message(const UserCredentials& creds, AuthMode mode)
{
    // Protocol format:
    //   "/register <username>|<password>\n"  (cmd_type 2 on server)
    //   "/login    <username>|<password>\n"  (cmd_type 1 on server)
    if (mode == AuthMode::REGISTER)
        return "/register " + creds.username + "|" + creds.password + "\n";
    else
        return "/login "    + creds.username + "|" + creds.password + "\n";
}

// ---------------------------------------------------------------------------
// register_user — legacy error-triggered re-registration flow
// ---------------------------------------------------------------------------
void TcpClient::register_user(int server_socket, Errortype type)
{
    if (type == Errortype::Error_101)
        std::cout << "[Error101] This username is already in use\n";

    // register_username() returns "username|password" string
    username = register_username();
    std::string greeting = "/username " + username + "\n";
    send(server_socket, greeting.c_str(), greeting.size(), 0);
}

// ---------------------------------------------------------------------------
// register_username — standalone interactive prompt (legacy)
// ---------------------------------------------------------------------------
std::string TcpClient::register_username()
{
    std::string name;
    std::string passwd;

    // Username prompt loop
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

    // Password prompt loop
    while (true) {
        std::cout << "Enter your password: ";
        std::cout.flush();
        std::getline(std::cin, passwd);
        trim(passwd);

        if (passwd.empty()) {
            std::cout << "Password cannot be empty.\n";
            continue;
        }
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

    // Return combined "username|password" for use in the protocol message
    return name + "|" + passwd;
}

// ===========================================================================
// main — entry point for the chat client
// ===========================================================================
int main()
{
    int port = 25565;

    // Prompt for the server IP, validated as an IPv4 address
    std::string server_ip = getString("Enter server ip address: ",
                                      false, true, StringType::IPV4);

    // Construct client (creates socket); local IP is informational only
    TcpClient client(port, server_ip.c_str());

    // Retry connection until success (network may be temporarily unavailable)
    while (true) {
        int conn_result = client.connect_and_authenticate(server_ip.c_str());
        if (conn_result == 0) break;
        std::cout << "Retrying connection in 5 seconds...\n";
        sleep(5);
        continue;
    }

    int sockfd = client.getClientFd();

    // Save original terminal settings before switching to raw mode
    tcgetattr(STDIN_FILENO, &old_term);
    setup_stdin(); // disable ICANON + ECHO; set stdin non-blocking

    fd_set read_fds;
    int max_fd = std::max(STDIN_FILENO, sockfd); // needed by select()

    // Authentication already succeeded inside connect_and_authenticate()
    bool registered = true;
    username = client.username;

    // Show initial prompt
    std::cout << username << "> ";
    std::cout.flush();

    std::string socket_buffer; // accumulates partial lines from the server

    // ── Main I/O loop ────────────────────────────────────────────────────────
    while (true)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds); // watch keyboard input
        FD_SET(sockfd, &read_fds);       // watch server messages

        // Block until either stdin or the socket is ready for reading
        if (select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr) < 0) {
            if (errno == EINTR) continue; // signal interrupted select — retry
            perror("select");
            break;
        }

        // ── Handle keyboard input ────────────────────────────────────────────
        if (registered && FD_ISSET(STDIN_FILENO, &read_fds))
            handle_stdin(sockfd);

        // ── Handle incoming server data ──────────────────────────────────────
        if (FD_ISSET(sockfd, &read_fds))
        {
            char buf[1024]{};
            ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);

            if (n <= 0) {
                // n == 0: server closed connection; n < 0: socket error
                std::cout << "\nServer disconnected.\n";
                restore_stdin();
                close(sockfd);
                return 0;
            }

            buf[n] = '\0';
            socket_buffer.append(buf, n); // append to partial-message buffer

            // Process all complete lines (delimited by '\n')
            size_t pos;
            while ((pos = socket_buffer.find('\n')) != std::string::npos)
            {
                std::string line = socket_buffer.substr(0, pos + 1);
                socket_buffer.erase(0, pos + 1); // consume the processed line

                // \r\033[K: carriage-return + clear-to-end-of-line (ANSI escape)
                // This overwrites the current prompt line before printing the
                // incoming message, then redraws the prompt + typed input below
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

// ---------------------------------------------------------------------------
// restore_stdin — restores terminal to the state saved before setup_stdin()
// ---------------------------------------------------------------------------
void restore_stdin()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);            // restore cooked mode
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags);     // restore blocking I/O
}

// ---------------------------------------------------------------------------
// setup_stdin — switches terminal to raw mode + non-blocking stdin
// ---------------------------------------------------------------------------
void setup_stdin()
{
    // Save current fcntl flags so restore_stdin() can put them back
    original_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);

    // O_NONBLOCK: read() on stdin returns immediately with EAGAIN if no data,
    // allowing the select() loop to keep processing socket events without blocking
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags | O_NONBLOCK);

    termios new_term = old_term;

    // ICANON: disables line buffering — characters available immediately, not on '\n'
    // ECHO:   disables automatic echo — we echo manually for backspace support
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
}

// ---------------------------------------------------------------------------
// handle_stdin — reads raw keypresses and manages the input_buffer
// ---------------------------------------------------------------------------
void handle_stdin(int sockfd)
{
    char c;

    // Drain all available characters from stdin (non-blocking loop)
    while (true)
    {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) break; // EAGAIN (no data) or error — stop draining

        // ── Enter key: submit message ────────────────────────────────────────
        if (c == '\n' || c == '\r')
        {
            if (!input_buffer.empty())
            {
                // Built-in commands handled locally without sending to server
                if (input_buffer == "/clear") {
                    system("clear"); // clear terminal screen
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

                // Append newline as message terminator (server reads until '\n')
                input_buffer += '\n';
                send(sockfd, input_buffer.c_str(), input_buffer.size(), 0);
                input_buffer.clear();
            }

            // Redraw prompt on a new line
            std::cout << "\n" << username << "> ";
            std::cout.flush();
            continue;
        }

        // ── Backspace / DEL key ──────────────────────────────────────────────
        if (c == 127 || c == '\b')
        {
            if (!input_buffer.empty()) {
                input_buffer.pop_back();
                // "\b \b": move cursor back, overwrite with space, move back again
                std::cout << "\b \b";
                std::cout.flush();
            }
            continue;
        }

        // ── Printable ASCII only (0x20–0x7E) ─────────────────────────────────
        // Filters out escape sequences, control chars, and non-ASCII bytes
        if (c >= 32 && c < 127) {
            input_buffer += c;
            std::cout << c;  // manual echo since ECHO is disabled
            std::cout.flush();
        }
    }
}

