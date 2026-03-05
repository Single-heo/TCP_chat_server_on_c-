# TCP Chat Server (C++) — epoll-based

A multi-client TCP chat server written in **C++** using **Linux sockets** and **epoll** for scalable I/O multiplexing. The project is organized into modular components covering the server, client, database layer, and shared utilities — all built with **CMake**.

---

## 🚀 Features

- Multi-client TCP chat server
- Uses **epoll** for event-driven I/O
- Non-blocking sockets
- Graceful handling of client disconnects
- Username registration
- Message broadcasting to all connected clients
- Signal-safe (`SIGPIPE` ignored)
- **Database layer** for persistent storage
- Modular architecture with shared `common/` utilities
- CMake-based build system
- Designed for Linux

---

## 🧠 Why epoll instead of select?

| `select()` | `epoll()` |
|---|---|
| O(n) scan every call | O(1) event notification |
| fd limit (`FD_SETSIZE`) | Virtually unlimited |
| Copies fd sets each time | Kernel-managed interest list |
| Poor scalability | Excellent for many clients |

`epoll` is the **industry-standard** choice for high-performance Linux servers.

---

## 🗂️ Project Structure

```
.
├── Server-side/          # Server source code
├── Client-side/          # Client source code
├── DataBase/             # Database integration layer
├── common/               # Shared headers and utilities
├── CMakeLists.txt        # CMake build configuration
├── .gitignore
└── README.md
```

---

## ⚙️ Build

### Requirements

- Linux (epoll is Linux-specific)
- g++ (C++17 or newer)
- CMake 3.x+

### Compile with CMake

```bash
mkdir build && cd build
cmake ..
make
```

Binaries will be placed in the `build/` directory after compilation.

---

## ▶️ Run

### Start the server

```bash
./server
```

### Start one or more clients

```bash
./client
```

Each client will be prompted for a username before joining the chat.

---

## 🧩 epoll Design Overview

### epoll lifecycle

1. Create epoll instance
2. Register server socket
3. Wait for events using `epoll_wait()`
4. Accept new connections or read client data
5. Broadcast messages to all connected clients
6. Remove disconnected clients

### Key syscalls used

- `epoll_create1()`
- `epoll_ctl()`
- `epoll_wait()`
- `fcntl()` (non-blocking mode)
- `accept()` / `recv()` / `send()`

---

## 🔄 Event Flow

- **Server socket ready** → Accept new client
- **Client socket ready** → Read incoming message
- **Client disconnects** → Remove fd from epoll + close socket

---

## 🗄️ Database Layer

The `DataBase/` module handles persistent storage. This includes storing user data or chat history. It is decoupled from the server logic through the shared `common/` interface, keeping the architecture clean and extensible.

---

## 🛡️ Safety & Stability

- `SIGPIPE` is ignored to prevent crashes on writes to closed sockets
- All sockets are set to non-blocking mode
- epoll events are centrally managed
- Modular design separates concerns across server, client, and database layers

---

## 🧪 Tested On

- Ubuntu / Debian-based distros
- Kernel 5.x+

---

## 📌 Notes

- This is a learning-focused project exploring low-level networking and system design in C++
- Not intended for production use without further hardening (TLS, authentication, etc.)

---

## 📜 License

MIT License

---

## 👤 Author

**Single-heo**
Refactored to epoll-based architecture with modular CMake project structure.

---
