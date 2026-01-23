#include "client_header.hpp"
#include "../../USEFULL-HEADERS/input.hpp"
#include <algorithm>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#define DUPLICATED_USERNAME_ERROR "101"
// Global variable to store original terminal settings
// Used to restore terminal state when program exi  s
termios old_term;

// Function declarations
void restore_stdin();
void setup_stdin();
void handle_stdin(int sockfd);

// Buffer to accumulate characters typed by user before sending
std::string input_buffer{};

// Store username for display in prompt
std::string username{};
static int original_stdin_flags;
int main()
{
    int port = 25565;
    TcpClient client(port, "127.0.0.1");
    client.connect_to_server("127.0.0.1");
    int sockfd = client.getClientFd();
    
    // SAVE original terminal settings FIRST (before any modifications)
    tcgetattr(STDIN_FILENO, &old_term);
    
    // Get username FIRST, while stdin is still in normal mode
    username = client.get_username();
    
    // Send username to server
    std::string greeting = "/username " + username + "\n";
    send(sockfd, greeting.c_str(), greeting.size(), 0);
    
    // NOW set up non-blocking, non-canonical stdin for chat
    setup_stdin();
    
    fd_set read_fds;
    int max_fd = std::max(STDIN_FILENO, sockfd);
    
    bool registered = false;  // Track registration status

    while (true)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);

        if (select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr) < 0)
        {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Only handle stdin after registration is complete
        if (registered && FD_ISSET(STDIN_FILENO, &read_fds))
            handle_stdin(sockfd);

        if (FD_ISSET(sockfd, &read_fds))
        {
            char buf[1024];
            ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);

            if (n <= 0)
            {
                std::cout << "\nServer disconnected.\n";
                restore_stdin();
                close(sockfd);
                exit(0);
            }
            
            // Check for username error
            if (bufferEndsWith(buf, n, DUPLICATED_USERNAME_ERROR))
            {
                // Temporarily restore canonical mode for get_username()
                restore_stdin();
                
                std::cout << "[Error101] This username is already in use. Please try another.\n";
                username = client.get_username();
                
                // Send new username
                std::string new_greeting = "/username " + username + "\n";
                send(sockfd, new_greeting.c_str(), new_greeting.size(), 0);
                
                // Re-enable non-canonical mode for chat
                setup_stdin();
                continue;
            }
            
            // If we get here and weren't registered, we are now!
            if (!registered)
            {
                registered = true;
                std::cout << username << "> ";
                std::cout.flush();
                continue;  // Don't display the server's response as a message
            }
            
            buf[n] = '\0';
            std::cout << "\n" << buf << username << "> ";
            std::cout.flush();
        }
    }

    restore_stdin();
    close(sockfd);
    return 0;
}


void setup_stdin()
{
    // Save original flags first
    original_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    
    // Make non-blocking
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags | O_NONBLOCK);

    // Make stdin non-blocking so read() returns immediately if no data available
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Create modified terminal settings (old_term was already saved in main)
    termios new_term = old_term;
    
    // Disable canonical mode (line buffering)
    // This allows reading input character-by-character instead of line-by-line
    new_term.c_lflag &= ~(ICANON);
    
    // Disable automatic echo since we're manually echoing in handle_stdin()
    // This prevents double-echo of characters
    new_term.c_lflag &= ~(ECHO);

    // Apply the new terminal settings immediately
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
}

void restore_stdin()
{
    // Restore the original terminal settings saved in setup_stdin()
    // This ensures the terminal behaves normally after program exits
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    // Restore original flags
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags);
}

void handle_stdin(int sockfd)
{
    char c;

    // Read all available characters from stdin
    while (true)
    {
        // Attempt to read one character
        ssize_t r = read(STDIN_FILENO, &c, 1);

        // If no more data available or error occurred, exit loop
        if (r <= 0)
            break;

        // Handle ENTER key - send the buffered input to server
        if (c == '\n' || c == '\r')
        {
            // Only send if there's actually something in the buffer
            if (!input_buffer.empty())
            {
                if (bufferStartsWith(input_buffer.c_str(), input_buffer.size(), "/clear")){
                    system("clear");
                    input_buffer.clear();
                    continue; 
                }
                // Send the accumulated input to the server
                input_buffer += '\n';
                send(sockfd, input_buffer.c_str(), input_buffer.size(), 0);
                
                // Clear the buffer for next input
                input_buffer.clear();
            }
            
            // Print newline and new prompt with username
            std::cout << "\n" << username << "> ";
            std::cout.flush();
            continue;
        }

        // Handle BACKSPACE - remove last character from buffer
        if (c == 127 || c == '\b')
        {
            if (!input_buffer.empty())
            {
                // Remove last character from buffer
                input_buffer.pop_back();
                
                // Erase the character from terminal display
                // \b moves cursor back, space overwrites char, \b moves back again
                std::cout << "\b \b";
                std::cout.flush();
            }
            continue;
        }

        // Handle normal printable character
        // Add to buffer and echo to screen
        input_buffer += c;

        
        std::cout << c;
        std::cout.flush();
    }
}

