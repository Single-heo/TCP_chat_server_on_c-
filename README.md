># TCP Chat Application

A multi-client TCP chat application written in C++ using socket programming and CMake build system.

## Features

- **Multi-client support** using `select()` for I/O multiplexing
- **Username registration** with duplicate detection
- **Real-time message broadcasting** to all connected clients
- **Non-blocking terminal input** for smooth chat experience
- **Proper terminal mode management** with automatic restoration
- **CMake build system** for easy compilation

## Requirements

- C++17 or later
- CMake 3.10 or later
- Linux/Unix system (uses POSIX sockets and termios)
- GCC or Clang compiler

## Project Structure

```
.
├── CMakeLists.txt              # CMake build configuration
├── Client-side/
│   ├── Client-main.cpp         # Client application entry point
│   └── client_header.hpp       # Client class definitions
├── Server-side/
│   ├── Server_main.cpp         # Server application entry point
│   ├── server-header.hpp       # Server class declarations
│   └── server_header_definition.cpp  # Server class implementations
├── common/
│   └── input.hpp               # Shared input utility functions
└── README.md
```

## Building

### Using CMake (Recommended)

```bash
# Clone the repository
git clone https://github.com/Single-heo/TCP_chat_server_on_c-.git
cd TCP_chat_server_on_c-

# Create build directory
mkdir build
cd build

# Generate build files
cmake ..

# Compile
make -j$(nproc)

# Executables will be in the build directory:
# - ./server
# - ./client
```

### Clean Rebuild

```bash
# From project root
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Build Types

```bash
# Debug build (with debugging symbols)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Manual Compilation (Alternative)

```bash
# Compile server
g++ -o server Server-side/Server_main.cpp Server-side/server_header_definition.cpp -std=c++17 -I./common

# Compile client
g++ -o client Client-side/Client-main.cpp -std=c++17 -I./common
```

## Usage

### Start the Server

```bash
# From the build directory
./server
```

The server will start listening on `127.0.0.1:25565` by default.

### Connect Clients

Open additional terminals and run:

```bash
# From the build directory
./client
```

You'll be prompted to enter a username. Once connected, you can start chatting!

### Chat Commands

- Type your message and press **Enter** to send
- `/clear` - Clear the terminal screen
- **Ctrl+C** - Disconnect and exit

## Architecture

### Server Design

- Uses `select()` system call for I/O multiplexing to handle multiple clients simultaneously
- Non-blocking socket I/O prevents server from freezing
- Maintains a `std::unordered_map` of connected clients
- Tracks usernames in a `std::unordered_set` to prevent duplicates
- Broadcasts messages to all clients except the sender

### Client Design

- Non-canonical terminal mode for character-by-character input
- Non-blocking stdin allows simultaneous reading and writing
- Properly saves and restores terminal settings on exit
- Handles duplicate username errors with re-registration flow

### Protocol

- Username registration: `/username <name>\n`
- Error codes: `101` - Duplicate username
- Messages are broadcast with format: `username: message\n`

## Technical Details

### Key Technologies

- **POSIX Sockets** - TCP/IP communication
- **select()** - I/O multiplexing for handling multiple connections
- **termios** - Terminal I/O control for non-canonical mode
- **fcntl()** - File descriptor flag manipulation for non-blocking I/O

### Thread Safety

Currently single-threaded using `select()` for multiplexing. All client operations are handled in the main server loop.

## Development

### After Making Changes

```bash
cd build
make -j$(nproc)
```

CMake will automatically detect which files changed and recompile only what's necessary.

## Known Limitations

- Server binds to localhost only (127.0.0.1)
- No message history or persistence
- No encryption (messages sent in plain text)
- Maximum 7 simultaneous clients (configurable in code)

## Future Enhancements

- [ ] Add SSL/TLS encryption
- [ ] Implement private messaging
- [ ] Add message history
- [ ] Support for custom server IP and port
- [ ] Add logging system
- [ ] Implement chat rooms/channels

## Contributing

Feel free to open issues or submit pull requests!

## License

This project is open source and available for educational purposes.

## Author

Created as a learning project for network programming and socket APIs in C++
