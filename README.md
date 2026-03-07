# TCP Chat Application

A multi-client TCP chat application written in C++ using socket programming and CMake.

---

## What's New

### Authentication System
The server now supports full account registration and login. Clients must authenticate before sending messages.

- `/register <username>|<password>` — creates a new account, persisted to `DataBase/credentials.json`
- `/login <username>|<password>` — authenticates against stored credentials
- Credentials are validated for length, empty fields, and forbidden characters before being sent to the server
- Server responds with a success or `Error:` message; the client exits on auth failure after 3 attempts

### Credential Persistence (JSON Database)
User accounts are saved to `DataBase/credentials.json` using [nlohmann/json](https://github.com/nlohmann/json). Each entry stores a username, password, and `created_at` timestamp.

> ⚠️ Passwords are currently stored in plain text. Hashing (e.g. bcrypt) is planned.

### epoll-based Server (replaces `select()`)
The server I/O loop has been migrated from `select()` to Linux `epoll`, improving scalability for concurrent clients.

- `epoll_create1`, `epoll_ctl`, `epoll_wait` replace `FD_SET` / `select()`
- Clients are registered/deregistered dynamically via `add_to_epoll` / `remove_from_epoll`
- 1000 ms timeout on `epoll_wait` allows periodic `SERVER_IS_RUNNING` checks

### Client-side Commands
The client now supports in-chat slash commands:

| Command  | Description              |
|----------|--------------------------|
| `/clear` | Clear the terminal screen |
| `/exit`  | Disconnect and exit       |
| `/help`  | Show available commands   |

Unknown commands beginning with `/` print a local error and are not sent to the server. Regular messages are sent normally.

### Improved Input Handling
- `verify_command` now correctly uses `else if` chains — previously all branches were independent `if` statements, causing regular chat messages to never be sent
- Printable ASCII filter in `handle_stdin` rejects control characters, escape sequences, and non-ASCII bytes
- Backspace renders correctly with `\b \b` (move back, erase, move back)

### Wire Protocol Update

| Direction       | Format                                  |
|-----------------|-----------------------------------------|
| Register        | `/register <username>\|<password>\n`    |
| Login           | `/login <username>\|<password>\n`       |
| Chat message    | `<message>\n`                           |
| Server broadcast| `<username>: <message>\n`               |

---

## Features

- Multi-client support via `epoll` I/O multiplexing
- Account registration and login with JSON persistence
- Credential validation (length, empty fields, reserved characters)
- Real-time message broadcasting to all connected clients
- Non-blocking terminal input with raw mode (no line buffering)
- Slash-command support with local dispatch
- Graceful client disconnection and username slot release

---

## Requirements

- C++17 or later
- CMake 3.10 or later
- Linux (uses `epoll`, POSIX sockets, `termios`)
- GCC or Clang
- [nlohmann/json](https://github.com/nlohmann/json) (header-only, included or installed via package manager)

---

## Project Structure

```
.
├── CMakeLists.txt
├── DataBase/
│   └── credentials.json          # Auto-created on first registration
├── Client-side/
│   ├── Client-main.cpp           # Client entry point + main I/O loop
│   └── client_header.hpp         # TcpClient class definition
├── Server-side/
│   ├── Server_main.cpp           # Server entry point
│   ├── server-header.hpp         # TcpServer class declaration
│   └── server_header_definition.cpp  # TcpServer implementation
├── common/
│   └── input.hpp                 # Shared utilities: buffer helpers, credential parser
└── README.md
```

---

## Building

### Using CMake (Recommended)

```bash
git clone https://github.com/Single-heo/TCP_chat_server_on_c-.git
cd TCP_chat_server_on_c-

mkdir build && cd build
cmake ..
make -j$(nproc)
```

Executables will be placed in the `build/` directory as `./server` and `./client`.

### Build Types

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..    # with debug symbols
cmake -DCMAKE_BUILD_TYPE=Release ..  # optimized
```

### Clean Rebuild

```bash
rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)
```

---

## Usage

### 1. Start the server

```bash
./server
```

The server binds to `127.0.0.1:25565` by default.

### 2. Connect a client

```bash
./client
```

You will be prompted for the server IP, then asked to register or log in.

### 3. Chat

Type a message and press **Enter** to broadcast it. Use `/help` to see available commands.

---

## Architecture

### Server

- `epoll`-based event loop handles the server socket and all client fds in a single thread
- New connections are accepted in `handle_new_connection()` and rejected if `MAX_CLIENTS` (7) is reached
- Auth commands (`/register`, `/login`) are parsed before any chat message is processed
- Unauthenticated clients receive an error if they attempt to send chat messages
- Partial messages are buffered per client until a `\n` is received, then broadcast

### Client

- `select()` multiplexes stdin and the server socket in the main loop
- Terminal is switched to raw mode (`ICANON` + `ECHO` disabled) on connect and restored on exit
- `stdin` is set to `O_NONBLOCK` so keyboard reads never block the socket loop
- `verify_command` dispatches slash commands locally or forwards plain messages to the server

---

## Known Limitations

- Passwords are stored in plain text — hashing is not yet implemented
- Server binds to localhost only (`127.0.0.1`)
- No message history
- No encryption (plain text over TCP)
- Maximum 7 simultaneous clients

---

## Planned

- Password hashing (bcrypt or similar)
- Private messaging (`/msg <user> <text>`)
- Configurable bind address and port via CLI args
- Message history / scrollback
- SSL/TLS support
- Chat rooms / channels

---

## License

Open source — available for educational purposes.

## Author

Built as a learning project for network programming and POSIX socket APIs in C++.
