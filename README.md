# TCP Chat Application

A multi-client TCP chat server written in C++ using POSIX sockets, Linux `epoll`, and Argon2id password hashing via libsodium.

---

## Features

- Multi-client support via `epoll` I/O multiplexing (edge-triggered)
- Non-blocking sockets (`O_NONBLOCK`) on server and all client fds
- Account registration and login with JSON persistence
- **Argon2id password hashing** via libsodium — passwords are never stored in plain text
- Credential validation (length, empty fields, reserved characters)
- Real-time message broadcasting to all authenticated clients
- Partial message reassembly — buffers `recv()` chunks until `\n`
- Non-blocking terminal input with raw mode (no line buffering)
- Slash-command support with local client-side dispatch
- Graceful client disconnection and username slot release
- IP validation at startup — server refuses to bind to a foreign address

---

## Requirements

- C++17 or later
- CMake 3.10 or later
- Linux (uses `epoll`, POSIX sockets, `termios`, `ifaddrs`)
- GCC or Clang
- **libsodium** (`sudo apt install libsodium-dev`)
- [nlohmann/json](https://github.com/nlohmann/json) (header-only, included under `common/`)

---

## Project Structure with tree command

```
.
├── CMakeLists.txt
├── DataBase/
│   └── credentials.json              # Auto-created on first /register
├── Client-side/
│   ├── Client-main.cpp               # Client entry point + main I/O loop
│   └── client_header.hpp             # TcpClient class definition
├── Server-side/
│   ├── Server_main.cpp               # Server entry point + IP validation
│   ├── server-header.hpp             # TcpServer class declaration
│   └── server_header_definition.cpp  # TcpServer implementation
├── common/
│   ├── input.hpp                     # Shared utilities: buffer helpers, credential parser, input validation
│   └── nlohmann/                     # nlohmann/json header-only library
└── README.md
```

---

## Building

### Dependencies

#### Debian/Ubuntu 

<img width="20" height="22" alt="image" src="https://github.com/user-attachments/assets/0fa974f7-dcc4-4269-b083-3069af87e4c0"/>

```bash

sudo apt update
sudo apt install argon2-dev -y
sudo apt install libsodium-dev -y
```

#### Arch
<img width="20" height="20" alt="image" src="https://github.com/user-attachments/assets/e994402a-a449-4141-b55f-75ed3b83c308" />

```bash
# Arch
sudo pacman -S base-devel cmake pkgconf libsodium argon2
```

### CMake (recommended)

```bash
git clone https://github.com/Single-heo/TCP_chat_server_on_c-.git
cd TCP_chat_server_on_c-

mkdir build && cd build
cmake ..
make -j$(nproc)
```

Executables are placed in `build/` as `./server` and `./client`.

### Build types

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..    # debug symbols
cmake -DCMAKE_BUILD_TYPE=Release ..  # optimized
```

### Clean rebuild

```bash
rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)
```

---

## Usage

### 1. Start the server

```bash
./server
```

You will be prompted for a local IP address and port. The server validates that the IP belongs to a local interface before binding.

### 2. Connect a client

```bash
./client
```

You will be prompted for the server IP and port, then asked to `/register` or `/login`.

### 3. Chat

Type a message and press **Enter** to broadcast it to all other authenticated clients. Use `/help` to list available commands.

---

## Wire Protocol

| Direction        | Format                               |
|------------------|--------------------------------------|
| Register         | `/register <username>\|<password>\n` |
| Login            | `/login <username>\|<password>\n`    |
| Chat message     | `<message>\n`                        |
| Server broadcast | `<username>: <message>\n`            |

---

## Client Commands

| Command  | Description                |
|----------|----------------------------|
| `/register <user>\|<pass>` | Create a new account |
| `/login <user>\|<pass>`    | Authenticate         |
| `/clear` | Clear the terminal screen  |
| `/exit`  | Disconnect and exit        |
| `/help`  | Show available commands    |

Unknown commands beginning with `/` print a local error and are not forwarded to the server.

---

## Architecture

### Server

- `epoll`-based single-threaded event loop monitors the server socket and all client fds
- Server socket and all client sockets are set to `O_NONBLOCK` via `fcntl()`
- New connections are accepted in `handle_new_connection()` and rejected with an error message if `MAX_CLIENTS` (7) is reached
- Auth commands (`/register`, `/login`) are parsed and dispatched before any chat message is processed
- Unauthenticated clients receive an error if they attempt to send chat messages
- Partial messages are accumulated per client in `write_buffer` until a `\n` is received, then broadcast via `sendAll()`
- `sendAll()` loops on `send()` to handle partial writes on non-blocking sockets
- Failed broadcast targets are collected and disconnected after the iteration to avoid iterator invalidation
- `SIGPIPE` is ignored server-wide so that writing to a closed socket returns `-1` instead of terminating the process

### Client

- `select()` multiplexes stdin and the server socket in the main loop
- Terminal is switched to raw mode (`ICANON` + `ECHO` disabled) on connect and restored on exit
- stdin is set to `O_NONBLOCK` so keyboard reads never block the socket loop
- `verify_command` dispatches slash commands locally or forwards plain messages to the server

### Password Security

Passwords are hashed with **Argon2id** using libsodium's `crypto_pwhash_str` before being written to disk:

- Memory cost: 64 MB (`OPSLIMIT_INTERACTIVE`)
- Time cost: 2 iterations (`MEMLIMIT_INTERACTIVE`)
- Random salt embedded in the hash string — no separate salt management needed
- Verification uses `crypto_pwhash_str_verify` with constant-time comparison

Example entry in `DataBase/credentials.json`:

```json
{
  "username": "heitor",
  "password": "$argon2id$v=19$m=65536,t=2,p=1$...",
  "IP_source": "192.168.1.2",
  "created_at": "2026-04-24 17:56:37"
}
```

---

## Known Limitations

- No TLS/SSL — credentials and messages travel in plain text over the wire
- `recv()` does not loop on `EAGAIN` in edge-triggered mode (partial read possible under high load)
- Write buffering on `EAGAIN` from `send()` is not yet implemented
- No message history or scrollback
- Single-threaded — one slow client operation can delay others

---

## Planned

- TLS via mbedTLS or OpenSSL — encrypts the wire so credentials are not exposed
- Looping `recv()` until `EAGAIN` for correct edge-triggered behaviour
- Write buffer per client to handle `EAGAIN` on `send()` without dropping data
- Private messaging (`/msg <user> <text>`)
- Message history / scrollback
- Chat rooms / channels
- Configurable `MAX_CLIENTS` via CLI args

---

## License

Open source — available for educational purposes.

---

## Author

### Heitor Zanin Mello

Built as a learning project for network programming and POSIX socket APIs in C++.
