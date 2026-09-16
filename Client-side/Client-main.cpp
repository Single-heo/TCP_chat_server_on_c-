#include "client_header.hpp"
// input.hpp is already included transitively via client_header.hpp
#include <algorithm>
#include <unistd.h>      // read, close, sleep
#include <sys/select.h>  // select, FD_SET, FD_ZERO, FD_ISSET
#include <cerrno>        // errno, EINTR
#include <fcntl.h>       // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <termios.h>     // tcgetattr, tcsetattr, termios — raw terminal control
#include <cstdlib>       // getenv

#define DUPLICATED_USERNAME_ERROR "101"

// ── Module-level state ───────────────────────────────────────────────────────
static termios old_term;            // Original terminal settings (restored on exit)
static int original_stdin_flags;    // Original stdin fcntl flags (for O_NONBLOCK cleanup)
static std::string input_buffer{};  // Accumulates characters typed by the user
static std::string username{};      // Display name prepended to the prompt

// Forward declarations
void restore_stdin();
void setup_stdin();
void handle_stdin(int sockfd, TcpClient* client);

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
// init_ssl_ctx — builds the client's SSL_CTX with the right verification mode
// ---------------------------------------------------------------------------
bool TcpClient::init_ssl_ctx(const std::string& ca_cert_path)
{
    const SSL_METHOD* method = TLS_client_method();
    ssl_ctx = SSL_CTX_new(method);
    if (!ssl_ctx) {
        std::cerr << "Failed to create SSL context\n";
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Refuse to negotiate anything below TLS 1.2
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

    if (!ca_cert_path.empty()) {
        if (SSL_CTX_load_verify_locations(ssl_ctx, ca_cert_path.c_str(), nullptr) != 1) {
            std::cerr << "Failed to load CA certificate: " << ca_cert_path << "\n";
            ERR_print_errors_fp(stderr);
            return false;
        }
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
    } else {
        std::cerr << "[WARNING] TLS certificate verification is DISABLED "
                     "(no CA cert provided). Anyone able to intercept the "
                     "connection could impersonate the server. Set the "
                     "CHAT_CA_CERT environment variable to your server's "
                     "CA/cert PEM file to enable verification.\n";
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);
    }

    return true;
}

// ---------------------------------------------------------------------------
// tls_handshake — performs the TLS client handshake over client_fd
// ---------------------------------------------------------------------------
bool TcpClient::tls_handshake(const std::string& ca_cert_path)
{
    if (!init_ssl_ctx(ca_cert_path))
        return false;

    ssl = SSL_new(ssl_ctx);
    if (!ssl) {
        std::cerr << "Failed to create SSL object\n";
        ERR_print_errors_fp(stderr);
        return false;
    }

    if (SSL_set_fd(ssl, client_fd) != 1) {
        std::cerr << "Failed to bind SSL object to socket\n";
        ERR_print_errors_fp(stderr);
        return false;
    }

    // SNI — harmless for an IP-addressed dev server, useful once you put
    // this behind a real hostname
    SSL_set_tlsext_host_name(ssl, server_ip.c_str());

    int ret = SSL_connect(ssl);
    if (ret != 1) {
        int err = SSL_get_error(ssl, ret);
        std::cerr << "TLS handshake failed (SSL error code " << err << ")\n";
        ERR_print_errors_fp(stderr);
        return false;
    }

    std::cout << "✓ TLS handshake complete — " << SSL_get_version(ssl)
              << ", cipher " << SSL_get_cipher(ssl) << "\n";

    return true;
}

// ---------------------------------------------------------------------------
// tls_send / tls_recv — thin wrappers around SSL_write / SSL_read
// ---------------------------------------------------------------------------
ssize_t TcpClient::tls_send(const void* buf, size_t len)
{
    if (!ssl) return -1;

    int ret = SSL_write(ssl, buf, static_cast<int>(len));
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        std::cerr << "TLS write error (code " << err << ")\n";
        return -1;
    }
    return ret;
}

ssize_t TcpClient::tls_recv(void* buf, size_t len)
{
    if (!ssl) return -1;

    int ret = SSL_read(ssl, buf, static_cast<int>(len));
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_ZERO_RETURN)
            return 0; // peer sent close_notify — treat like recv() returning 0
        return -1;
    }
    return ret;
}

// ---------------------------------------------------------------------------
// connect_and_authenticate
// Establishes the TCP connection, performs the TLS handshake, then runs the
// register/login handshake over the encrypted channel.
// Returns 0 on success, -1 on failure.
// ---------------------------------------------------------------------------
int TcpClient::connect_and_authenticate(const char* server_ipv4_address,
                                         const std::string& ca_cert_path)
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

    // ── TLS handshake ─────────────────────────────────────────────────────
    // Everything below this point (auth + chat) rides on the encrypted
    // channel instead of the raw socket.
    if (!tls_handshake(ca_cert_path)) {
        std::cerr << "Could not establish a secure connection to "
                  << server_ip << ":" << port << std::endl;
        close(client_fd);
        client_fd = -1;
        return -1;
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

    // ── Send credentials to server (over TLS) ────────────────────────────────
    std::string auth_msg = format_auth_message(creds, mode);
    if (tls_send(auth_msg.c_str(), auth_msg.length()) <= 0) {
        std::cerr << "Failed to send credentials over TLS\n";
        return -1;
    }

    // ── Read server response (over TLS) ──────────────────────────────────────
    char response[256]{};
    ssize_t n = tls_recv(response, sizeof(response) - 1);

    if (n > 0) {
        response[n] = '\0';

        // "Registered" is 10 characters — check prefix to handle trailing newline
        if (strncmp(response, "Registered", 10) == 0) {
            std::cout << "\n✓ Authentication successful!\n";
            username = creds.username; // store for use in the chat loop
        }else if (strncmp(response, "Login successful", 16) == 0) {
            std::cout << "\n✓ Login successful!\n";
            username = creds.username; // store for use in the chat loop
        }
        else if (strncmp(response, "[ERROR]: ", 9) == 0) {
            system("clear");
            std::cerr << "\n✗ " << response;
            std::cerr <<  BOLD << RED << "\n[ERROR]: " << NC << "Authentication failed. Please restart the client and try again.\n";
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
// verify_command — handles slash-commands or sends regular chat messages
// (chat messages now go out over the TLS session via tls_send)
// ---------------------------------------------------------------------------
void TcpClient::verify_command(std::string *input_buffer, int sockfd)
{
    if (input_buffer->substr(0, 6) == "/clear") {
        system("clear");
        input_buffer->clear();
    }
    else if (input_buffer->substr(0, 5) == "/exit") {
        restore_stdin();
        std::cout << "\nExiting chat client.\n";
        close(sockfd);
        exit(0);
    }
    else if (input_buffer->substr(0, 5) == "/help") {
        std::cout << "\nAvailable commands:\n";
        std::cout << "  /clear - Clear the chat screen\n";
        std::cout << "  /exit  - Exit the chat client\n";
        std::cout << "  /help  - Show this help message\n";
        input_buffer->clear();
    }
    else if (!input_buffer->empty() && input_buffer->front() == '/') {
        std::cout << "\nError: unknown command '" << *input_buffer << "'\n";
        input_buffer->clear();
    }
    else {
        // Regular chat message — send to server over TLS
        input_buffer->append("\n");
        tls_send(input_buffer->c_str(), input_buffer->size());
        input_buffer->clear();
    }
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
    tls_send(greeting.c_str(), greeting.size());
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
    // Initialize OpenSSL (safe/no-op to call more than once on 1.1+/3.x)
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);

    int port = 25565;

    // Prompt for the server IP, validated as an IPv4 address
    std::string server_ip = getString("Enter server ip address: ",
                                      false, true, StringType::IPV4);

    // Optional: path to the server's CA/cert PEM file for verification.
    // Leave CHAT_CA_CERT unset only for local dev against a self-signed cert.
    std::string ca_cert_path;
    if (const char* env_ca = std::getenv("CHAT_CA_CERT")) {
        ca_cert_path = env_ca;
    }

    // Construct client (creates socket); local IP is informational only
    TcpClient client(port, server_ip.c_str());

    // Retry connection until success (network may be temporarily unavailable)
    while (true) {
        int conn_result = client.connect_and_authenticate(server_ip.c_str(), ca_cert_path);
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
            handle_stdin(sockfd, &client);

        // ── Handle incoming server data (over TLS) ───────────────────────────
        if (FD_ISSET(sockfd, &read_fds))
        {
            char buf[1024]{};
            ssize_t n = client.tls_recv(buf, sizeof(buf) - 1);

            if (n <= 0) {
                // n == 0: server closed connection (or sent close_notify);
                // n < 0: TLS/socket error
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
// Called each time select() signals stdin is ready for reading.
// Because stdin is non-blocking (O_NONBLOCK), we drain ALL available
// characters in a tight loop rather than reading just one per select wakeup.
// ---------------------------------------------------------------------------
void handle_stdin(int sockfd, TcpClient* client)
{
    char c;

    // Drain every character currently available on stdin.
    // read() returns <= 0 when the buffer is empty (EAGAIN) or on error,
    // at which point we stop and hand control back to the select() loop.
    while (true)
    {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) break; // EAGAIN (no more data) or unrecoverable error

        // ── Enter / Return key: submit whatever is in the buffer ─────────────
        if (c == '\n' || c == '\r')
        {
            if (!input_buffer.empty()) {
                // Route the buffer through verify_command:
                //   • slash-commands (/clear, /exit, /help) are handled locally
                //   • unknown slash-commands print an error
                //   • plain text is sent to the server as a chat message (over TLS)
                client->verify_command(&input_buffer, sockfd);
            }

            // Always redraw the prompt on a fresh line, even for empty Enter presses,
            // so the terminal doesn't look broken after the user hits Enter
            std::cout << "\n" << username << "> ";
            std::cout.flush();
            continue;
        }

        // ── Backspace (127 = DEL on most terminals) or legacy \b ────────────
        if (c == 127 || c == '\b')
        {
            if (!input_buffer.empty()) {
                input_buffer.pop_back(); // remove last logical character

                // Terminal sequence to visually erase the character:
                //   \b   — move cursor one position left
                //   ' '  — overwrite the character with a space
                //   \b   — move cursor left again, ready for next input
                std::cout << "\b \b";
                std::cout.flush();
            }
            // If buffer is already empty, silently ignore the backspace
            // (no bell or error — keeps UX clean)
            continue;
        }

        // ── Printable ASCII characters (0x20 space … 0x7E tilde) ────────────
        // Rejects:
        //   • control characters (0x00–0x1F): escape sequences, Ctrl+C, etc.
        //   • DEL (0x7F): already handled above
        //   • high bytes (0x80+): multi-byte UTF-8 sequences we don't support
        if (c >= 32 && c < 127) {
            input_buffer += c;  // append to logical buffer

            // Manual echo: ECHO is disabled in raw mode (setup_stdin),
            // so we must write each character ourselves to keep the
            // display in sync with input_buffer
            std::cout << c;
            std::cout.flush();
        }
    }
}
