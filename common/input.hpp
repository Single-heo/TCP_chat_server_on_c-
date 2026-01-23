#include <iostream>
#include <limits>
#include <string>
#include <regex>
#include <cctype>
#include <cstring>

// ============================================================================
// INPUT BUFFER MANAGEMENT
// ============================================================================

/**
 * Clears the input buffer in case of invalid input.
 * Used after failed input operations to prevent infinite loops.
 */
inline void clearInput()
{
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

// ============================================================================
// STRING VALIDATION TYPES
// ============================================================================

/**
 * Enum for string validation types.
 * Defines different validation patterns for input strings.
 */
enum class StringType
{
    ANY,         // Any string (no validation)
    IPV4,        // IPv4 address (e.g., 192.168.1.1)
    IPV6,        // IPv6 address (e.g., 2001:0db8:85a3::8a2e:0370:7334)
    EMAIL,       // Email address (e.g., user@example.com)
    ALPHANUMERIC // Only letters and numbers (no special characters)
};

// ============================================================================
// STRING VALIDATION FUNCTIONS
// ============================================================================

/**
 * Validates if a string is a valid IPv4 address.
 * Checks format: xxx.xxx.xxx.xxx where xxx is 0-255
 * @param str The string to validate
 * @return true if valid IPv4, false otherwise
 */
inline bool isValidIPv4(const std::string& str)
{
    std::regex ipv4_pattern(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"
    );
    return std::regex_match(str, ipv4_pattern);
}

/**
 * Validates if a string is a valid IPv6 address.
 * Supports standard, compressed, and IPv4-mapped IPv6 formats
 * @param str The string to validate
 * @return true if valid IPv6, false otherwise
 */
inline bool isValidIPv6(const std::string& str)
{
    // Comprehensive IPv6 validation covering multiple formats
    std::regex ipv6_pattern(
        "^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|"  // Full format: 8 groups
        "([0-9a-fA-F]{1,4}:){1,7}:|"                  // :: at end
        "([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|" // :: in middle
        ":((:[0-9a-fA-F]{1,4}){1,7}|:)|"              // :: at start
        "fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|" // Link-local with zone ID
        "::(ffff(:0{1,4}){0,1}:){0,1}"                // IPv4-mapped IPv6
        "((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3}"
        "(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|"
        "([0-9a-fA-F]{1,4}:){1,4}:"                   // Mixed IPv6/IPv4 notation
        "((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3}"
        "(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$"
    );
    return std::regex_match(str, ipv6_pattern);
}

/**
 * Validates if a string is a valid email address.
 * Basic email format: local-part@domain.tld
 * @param str The string to validate
 * @return true if valid email format, false otherwise
 */
inline bool isValidEmail(const std::string& str)
{
    std::regex email_pattern(
        "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$"
    );
    return std::regex_match(str, email_pattern);
}

/**
 * Validates if a string contains only alphanumeric characters.
 * No spaces, special characters, or punctuation allowed
 * @param str The string to validate
 * @return true if only letters and numbers, false otherwise
 */
inline bool isAlphanumeric(const std::string& str)
{
    if (str.empty()) return false;
    
    for (char c : str)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            return false;
        }
    }
    return true;
}

/**
 * Returns appropriate error message for validation type.
 * @param type The StringType that failed validation
 * @return Descriptive error message string
 */
inline std::string getValidationError(StringType type)
{
    switch (type)
    {
        case StringType::IPV4:
            return "Error: invalid IPv4 address format (e.g., 192.168.1.1).\n";
        case StringType::IPV6:
            return "Error: invalid IPv6 address format (e.g., 2001:db8::1).\n";
        case StringType::EMAIL:
            return "Error: invalid email address format.\n";
        case StringType::ALPHANUMERIC:
            return "Error: input must contain only letters and numbers.\n";
        default:
            return "Error: invalid input.\n";
    }
}

/**
 * Validates string based on specified type.
 * Central validation dispatcher function
 * @param str The string to validate
 * @param type The validation type to apply
 * @return true if string passes validation, false otherwise
 */
inline bool validateString(const std::string& str, StringType type)
{
    switch (type)
    {
        case StringType::ANY:
            return true;
        case StringType::IPV4:
            return isValidIPv4(str);
        case StringType::IPV6:
            return isValidIPv6(str);
        case StringType::EMAIL:
            return isValidEmail(str);
        case StringType::ALPHANUMERIC:
            return isAlphanumeric(str);
        default:
            return false;
    }
}

// ============================================================================
// USER INPUT FUNCTIONS (std::string based)
// ============================================================================

/**
 * Reads an integer from stdin with validation.
 * Loops until valid input within range is provided
 * @param prompt Message to display to user
 * @param min Minimum acceptable value (inclusive)
 * @param max Maximum acceptable value (inclusive)
 * @return Valid integer within specified range
 */
inline int getInt(const std::string& prompt,
        int min = std::numeric_limits<int>::min(),
        int max = std::numeric_limits<int>::max())
{
    int value;
    while (true)
    {
        std::cout << prompt;
        
        // Check if input is actually an integer
        if (!(std::cin >> value))
        {
            std::cout << "Error: invalid integer input.\n";
            clearInput();
            continue;
        }
        
        // Check if value is within range
        if (value < min || value > max)
        {
            std::cout << "Error: value must be between "
                    << min << " and " << max << ".\n";
            clearInput();
            continue;
        }
        
        clearInput();
        return value;
    }
}

/**
 * Reads a double from stdin with validation.
 * Loops until valid input within range is provided
 * @param prompt Message to display to user
 * @param min Minimum acceptable value (inclusive)
 * @param max Maximum acceptable value (inclusive)
 * @return Valid double within specified range
 */
inline double getDouble(const std::string& prompt,
                double min = -std::numeric_limits<double>::infinity(),
                double max = std::numeric_limits<double>::infinity())
{
    double value;
    while (true)
    {
        std::cout << prompt;
        
        // Check if input is a valid floating-point number
        if (!(std::cin >> value))
        {
            std::cout << "Error: invalid floating-point input.\n";
            clearInput();
            continue;
        }
        
        // Check if value is within range
        if (value < min || value > max)
        {
            std::cout << "Error: value must be between "
                    << min << " and " << max << ".\n";
            clearInput();
            continue;
        }
        
        clearInput();
        return value;
    }
}

/**
 * Reads a string from stdin with comprehensive validation.
 * @param prompt Message to display to user
 * @param allowEmpty If true, empty strings are accepted
 * @param trim If true, removes leading/trailing whitespace
 * @param type Validation pattern to apply (IPv4, email, etc.)
 * @return Valid string meeting all specified criteria
 */
inline std::string getString(const std::string& prompt, 
                    bool allowEmpty = false,
                    bool trim = false,
                    StringType type = StringType::ANY)
{
    std::string value;
    while (true)
    {
        std::cout << prompt;
        
        // Read entire line (including spaces)
        if (!std::getline(std::cin, value))
        {
            std::cin.clear();
            continue;
        }
        
        // Optional whitespace trimming
        if (trim)
        {
            // Find first non-whitespace character
            size_t start = value.find_first_not_of(" \t\n\r");
            if (start == std::string::npos)
            {
                value.clear(); // All whitespace
            }
            else
            {
                // Find last non-whitespace and extract substring
                size_t end = value.find_last_not_of(" \t\n\r");
                value = value.substr(start, end - start + 1);
            }
        }
        
        // Check for empty input
        if (!allowEmpty && value.empty())
        {
            std::cout << "Error: input cannot be empty.\n";
            continue;
        }
        
        // Apply type-specific validation
        if (!validateString(value, type))
        {
            std::cout << getValidationError(type);
            continue;
        }
        
        return value;
    }
}

/**
 * Reads a yes/no response from stdin.
 * @param prompt Message to display (automatically appends " (y/n): ")
 * @return true for yes/y, false for no/n (case-insensitive)
 */
inline bool getYesNo(const std::string& prompt)
{
    while (true)
    {
        std::string input = getString(prompt + " (y/n): ", false, true);
        
        // Convert to lowercase for case-insensitive comparison
        for (char& c : input)
        {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        
        // Accept various affirmative responses
        if (input == "y" || input == "yes")
        {
            return true;
        }
        // Accept various negative responses
        if (input == "n" || input == "no")
        {
            return false;
        }
        
        std::cout << "Error: please enter 'y' or 'n'.\n";
    }
}

// ============================================================================
// SERVER-SIDE BUFFER VALIDATION FUNCTIONS (char* based)
// These work with raw C-style buffers instead of std::string for performance
// Used in network programming where data comes in as char arrays
// ============================================================================

/**
 * Trims whitespace from buffer in-place.
 * Removes leading and trailing whitespace, shifts content to start
 * @param buffer The buffer to trim (modified in-place)
 * @param length Current length of data in buffer
 * @return New length after trimming
 */
inline ssize_t trimBuffer(char* buffer, ssize_t length)
{
    if (length <= 0) return 0;
    
    // Find first non-whitespace character
    ssize_t start = 0;
    while (start < length && std::isspace(static_cast<unsigned char>(buffer[start])))
    {
        start++;
    }
    
    // Buffer contains only whitespace
    if (start == length)
    {
        buffer[0] = '\0';
        return 0;
    }
    
    // Find last non-whitespace character
    ssize_t end = length - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(buffer[end])))
    {
        end--;
    }
    
    // Calculate new length (inclusive range)
    ssize_t new_length = end - start + 1;
    
    // Shift trimmed content to beginning if necessary
    if (start > 0)
    {
        std::memmove(buffer, buffer + start, new_length);
    }
    
    // Null terminate the trimmed string
    buffer[new_length] = '\0';
    
    return new_length;
}

/**
 * Checks if buffer is empty or contains only whitespace.
 * @param buffer The buffer to check
 * @param length Length of data in buffer
 * @return true if empty or all whitespace, false otherwise
 */
inline bool isBufferEmpty(const char* buffer, ssize_t length)
{
    if (length <= 0) return true;
    
    // Check each character - if any is non-whitespace, buffer is not empty
    for (ssize_t i = 0; i < length; i++)
    {
        if (!std::isspace(static_cast<unsigned char>(buffer[i])))
        {
            return false;
        }
    }
    return true;
}

/**
 * Checks if buffer contains only alphanumeric characters.
 * @param buffer The buffer to check
 * @param length Length of data in buffer
 * @return true if all characters are letters or numbers, false otherwise
 */
inline bool isBufferAlphanumeric(const char* buffer, ssize_t length)
{
    if (length <= 0) return false;
    
    for (ssize_t i = 0; i < length; i++)
    {
        if (!std::isalnum(static_cast<unsigned char>(buffer[i])))
        {
            return false;
        }
    }
    return true;
}

/**
 * Checks if buffer contains only printable characters.
 * Excludes control characters like newlines, tabs, etc.
 * @param buffer The buffer to check
 * @param length Length of data in buffer
 * @return true if all characters are printable, false otherwise
 */
inline bool isBufferPrintable(const char* buffer, ssize_t length)
{
    if (length <= 0) return false;
    
    for (ssize_t i = 0; i < length; i++)
    {
        if (!std::isprint(static_cast<unsigned char>(buffer[i])))
        {
            return false;
        }
    }
    return true;
}

/**
 * Validates if buffer is a valid IPv4 address.
 * Converts to string internally for regex validation
 * @param buffer The buffer containing potential IPv4 address
 * @param length Length of data in buffer
 * @return true if valid IPv4 format, false otherwise
 */
inline bool isBufferIPv4(const char* buffer, ssize_t length)
{
    if (length <= 0) return false;
    
    // Create temporary string for regex matching
    std::string str(buffer, length);
    std::regex ipv4_pattern(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"
    );
    return std::regex_match(str, ipv4_pattern);
}

/**
 * Validates if buffer is a valid IPv6 address.
 * Converts to string internally for regex validation
 * @param buffer The buffer containing potential IPv6 address
 * @param length Length of data in buffer
 * @return true if valid IPv6 format, false otherwise
 */
inline bool isBufferIPv6(const char* buffer, ssize_t length)
{
    if (length <= 0) return false;
    
    // Create temporary string for regex matching
    std::string str(buffer, length);
    std::regex ipv6_pattern(
        "^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|"
        "([0-9a-fA-F]{1,4}:){1,7}:|"
        "([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|"
        ":((:[0-9a-fA-F]{1,4}){1,7}|:)|"
        "fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|"
        "::(ffff(:0{1,4}){0,1}:){0,1}"
        "((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3}"
        "(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|"
        "([0-9a-fA-F]{1,4}:){1,4}:"
        "((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3}"
        "(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$"
    );
    return std::regex_match(str, ipv6_pattern);
}

/**
 * Validates if buffer is a valid email address.
 * Converts to string internally for regex validation
 * @param buffer The buffer containing potential email address
 * @param length Length of data in buffer
 * @return true if valid email format, false otherwise
 */
inline bool isBufferEmail(const char* buffer, ssize_t length)
{
    if (length <= 0) return false;
    
    // Create temporary string for regex matching
    std::string str(buffer, length);
    std::regex email_pattern(
        "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$"
    );
    return std::regex_match(str, email_pattern);
}

/**
 * Sanitizes buffer by removing/replacing dangerous characters.
 * Keeps alphanumeric and safe punctuation, replaces others
 * @param buffer The buffer to sanitize (modified in-place)
 * @param length Current length of data in buffer
 * @param replacement Character to use for replacing unsafe chars
 * @return New length after sanitization
 */
inline ssize_t sanitizeBuffer(char* buffer, ssize_t length, char replacement = '_')
{
    if (length <= 0) return 0;
    
    ssize_t write_pos = 0;
    for (ssize_t i = 0; i < length; i++)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        
        // Keep safe characters: alphanumeric and common punctuation
        if (std::isalnum(c) || c == ' ' || c == '.' || c == ',' || 
            c == '!' || c == '?' || c == '-' || c == '_')
        {
            buffer[write_pos++] = buffer[i];
        }
        // Replace other printable characters with replacement char
        else if (std::isprint(c))
        {
            buffer[write_pos++] = replacement;
        }
        // Skip control characters entirely (don't include in output)
    }
    
    // Null terminate the sanitized string
    buffer[write_pos] = '\0';
    return write_pos;
}

/**
 * Checks if buffer length is within acceptable range.
 * Useful for preventing buffer overflow attacks
 * @param length Current buffer length to validate
 * @param min_len Minimum acceptable length
 * @param max_len Maximum acceptable length
 * @return true if length is within bounds, false otherwise
 */
inline bool isBufferLengthValid(ssize_t length, ssize_t min_len = 1, ssize_t max_len = 1024)
{
    return length >= min_len && length <= max_len;
}

/**
 * Converts buffer to lowercase in-place.
 * @param buffer The buffer to convert (modified in-place)
 * @param length Length of data in buffer
 */
inline void bufferToLower(char* buffer, ssize_t length)
{
    for (ssize_t i = 0; i < length; i++)
    {
        buffer[i] = std::tolower(static_cast<unsigned char>(buffer[i]));
    }
}

/**
 * Converts buffer to uppercase in-place.
 * @param buffer The buffer to convert (modified in-place)
 * @param length Length of data in buffer
 */
inline void bufferToUpper(char* buffer, ssize_t length)
{
    for (ssize_t i = 0; i < length; i++)
    {
        buffer[i] = std::toupper(static_cast<unsigned char>(buffer[i]));
    }
}

/**
 * Compares buffer with a string (case-sensitive).
 * @param buffer The buffer to compare
 * @param length Length of data in buffer
 * @param str The C-string to compare against
 * @return true if buffer and string match exactly, false otherwise
 */
inline bool bufferEquals(const char* buffer, ssize_t length, const char* str)
{
    size_t str_len = std::strlen(str);
    if (length != static_cast<ssize_t>(str_len)) return false;
    return std::memcmp(buffer, str, length) == 0;
}

/**
 * Compares buffer with a string (case-insensitive).
 * @param buffer The buffer to compare
 * @param length Length of data in buffer
 * @param str The C-string to compare against
 * @return true if buffer and string match (ignoring case), false otherwise
 */
inline bool bufferEqualsIgnoreCase(const char* buffer, ssize_t length, const char* str)
{
    size_t str_len = std::strlen(str);
    if (length != static_cast<ssize_t>(str_len)) return false;
    
    // Compare each character after converting to lowercase
    for (ssize_t i = 0; i < length; i++)
    {
        if (std::tolower(static_cast<unsigned char>(buffer[i])) != 
            std::tolower(static_cast<unsigned char>(str[i])))
        {
            return false;
        }
    }
    return true;
}

/**
 * Checks if buffer starts with a given prefix.
 * @param buffer The buffer to check
 * @param length Length of data in buffer
 * @param prefix The prefix string to check for
 * @return true if buffer starts with prefix, false otherwise
 */
inline bool bufferStartsWith(const char* buffer, ssize_t length, const char* prefix)
{
    size_t prefix_len = std::strlen(prefix);
    if (length < static_cast<ssize_t>(prefix_len)) return false;
    return std::memcmp(buffer, prefix, prefix_len) == 0;
}

/**
 * Checks if buffer ends with a given suffix.
 * @param buffer The buffer to check
 * @param length Length of data in buffer
 * @param suffix The suffix string to check for
 * @return true if buffer ends with suffix, false otherwise
 */
inline bool bufferEndsWith(const char* buffer, ssize_t length, const char* suffix)
{
    size_t suffix_len = std::strlen(suffix);
    if (length < static_cast<ssize_t>(suffix_len)) return false;
    return std::memcmp(buffer + length - suffix_len, suffix, suffix_len) == 0;
}

/**
 * Checks if buffer contains a substring.
 * @param buffer The buffer to search in
 * @param length Length of data in buffer
 * @param substring The substring to search for
 * @return true if substring is found, false otherwise
 */
inline bool bufferContains(const char* buffer, ssize_t length, const char* substring)
{
    if (length <= 0) return false;
    
    // Convert to string for easier substring search
    std::string buf_str(buffer, length);
    return buf_str.find(substring) != std::string::npos;
}

/**
 * Parses username from command buffer.
 * Expected format: "/username <actual_username>"
 * @param buffer The buffer containing the command
 * @param out_name Output buffer to store extracted username
 * @param out_size Size of output buffer
 * @return true if username was successfully parsed, false otherwise
 */
inline bool parse_username(const char* buffer, char* out_name, size_t out_size)
{
    const char* cmd = "/username ";
    
    // Check if buffer starts with "/username " command
    if (strncmp(buffer, cmd, strlen(cmd)) != 0)
        return false;
    
    // Get pointer to the actual username (after the command)
    const char* name_start = buffer + strlen(cmd);
    
    // Check if username is empty
    if (*name_start == '\0')
        return false;
    
    // Safely copy username to output buffer
    strncpy(out_name, name_start, out_size - 1);
    out_name[out_size - 1] = '\0'; // Ensure null termination
    
    return true;
}

// ============================================================================
// EXAMPLE USAGE IN SERVER
// ============================================================================

/*
// Example: Using buffer validation in a TCP server's run() function

ssize_t bytes_read = recv(current_fd, buffer, BUFFER_SIZE - 1, 0);

if (bytes_read <= 0) {
    // Client disconnected or error
    close(current_fd);
    FD_CLR(current_fd, &master_fds);
} 
else {
    // Null-terminate for safety
    buffer[bytes_read] = '\0';
    
    // Trim leading/trailing whitespace
    bytes_read = trimBuffer(buffer, bytes_read);
    
    // Check for empty message
    if (isBufferEmpty(buffer, bytes_read)) {
        std::cout << "Empty message from fd " << current_fd << ", ignoring\n";
        continue;
    }
    
    // Check for disconnect commands
    if (bufferEquals(buffer, bytes_read, "exit") || 
        bufferEquals(buffer, bytes_read, "quit")) {
        std::cout << "Client " << current_fd << " requested disconnect\n";
        const char* bye_msg = "Goodbye!\n";
        send(current_fd, bye_msg, strlen(bye_msg), 0);
        close(current_fd);
        FD_CLR(current_fd, &master_fds);
        continue;
    }
    
    // Check for help command (case-insensitive)
    if (bufferEqualsIgnoreCase(buffer, bytes_read, "help")) {
        const char* help_msg = "Available commands: exit, quit, help\n";
        send(current_fd, help_msg, strlen(help_msg), 0);
        continue;
    }
    
    // Validate message length (prevent attacks)
    if (!isBufferLengthValid(bytes_read, 1, 500)) {
        std::cout << "Invalid message length from fd " << current_fd << "\n";
        const char* error_msg = "Error: message too long or too short\n";
        send(current_fd, error_msg, strlen(error_msg), 0);
        continue;
    }
    
    // Check for username command
    char new_username[64];
    if (parse_username(buffer, new_username, sizeof(new_username))) {
        clients[current_fd].username = new_username;
        std::cout << "Client " << current_fd << " set username to: " 
                  << new_username << std::endl;
        continue;
    }
    
    // Check if message starts with command prefix
    if (bufferStartsWith(buffer, bytes_read, "/")) {
        std::cout << "Unknown command from fd " << current_fd << "\n";
        const char* error_msg = "Error: unknown command\n";
        send(current_fd, error_msg, strlen(error_msg), 0);
        continue;
    }
    
    // Valid message - log it
    std::cout << "Valid message from fd " << current_fd << ": " 
              << std::string(buffer, bytes_read) << std::endl;
    
    // Broadcast to other clients
    std::string full_msg = clients[current_fd].username + ": " 
                         + std::string(buffer, bytes_read);
    
    for (auto& [fd, client] : clients) {
        if (fd != current_fd) {
            send(fd, full_msg.c_str(), full_msg.size(), 0);
        }
    }
    
    // Clear buffer for next iteration
    memset(buffer, 0, BUFFER_SIZE);
}
*/

// ============================================================================
// EXAMPLE USAGE OF INPUT FUNCTIONS
// ============================================================================

/*
int main()
{
    // Get various types of validated input
    
    // Regular string with trimming
    std::string name = getString("Enter your name: ", false, true);
    
    // IPv4 address with validation
    std::string ipv4 = getString("Enter IPv4 address: ", false, true, StringType::IPV4);
    
    // IPv6 address with validation
    std::string ipv6 = getString("Enter IPv6 address: ", false, true, StringType::IPV6);
    
    // Email address with validation
    std::string email = getString("Enter email: ", false, true, StringType::EMAIL);
    
    // Alphanumeric username (no special chars)
    std::string username = getString("Enter username (alphanumeric only): ", 
                                    false, true, StringType::ALPHANUMERIC);
    
    // Integer with range validation
    int age = getInt("Enter your age (0-120): ", 0, 120);
    
    // Floating-point number
    double temperature = getDouble("Enter temperature (-50.0 to 50.0): ", -50.0, 50.0);
    
    // Yes/No question
    bool subscribe = getYesNo("Would you like to subscribe?");
    
    // Display results
    std::cout << "\nYour information:\n"
              << "Name: " << name << "\n"
              << "IPv4: " << ipv4 << "\n"
              << "IPv6: " << ipv6 << "\n"
              << "Email: " << email << "\n"
              << "Username: " << username << "\n"
              << "Age: " << age << '\n';
*/