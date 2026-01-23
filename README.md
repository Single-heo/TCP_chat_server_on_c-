# TCP Chat Application

A multi-client TCP chat application written in C++ using socket programming.

## Features
- Multi-client support using `select()` for I/O multiplexing
- Username registration with duplicate detection
- Real-time message broadcasting
- Non-blocking terminal input for smooth chat experience
- Proper terminal mode management

## Building
```bash
# Compile server
g++ -o server server.cpp -std=c++17

# Compile client
g++ -o client client.cpp -std=c++17
```

## Usage
```bash
# Start server
./server

# Start client (in another terminal)
./client
```

## Architecture
- Server uses `select()` to handle multiple clients simultaneously
- Client uses non-canonical terminal mode for character-by-character input
- Protocol includes username validation and error handling

## Requirements
- C++17 or later
- Linux/Unix system (uses POSIX sockets and termios)
