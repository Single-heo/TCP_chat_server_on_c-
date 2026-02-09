# TCP Chat Server (C++) — epoll-based

A multi-client TCP chat server written in **C++** using **Linux sockets** and **epoll** for scalable I/O multiplexing.
This project is an evolution of a `select()`-based server, refactored to use **epoll** for better performance and cleaner event-driven design.

---

## 🚀 Features

* Multi-client TCP chat server
* Uses **epoll** (edge-trigger friendly structure)
* Non-blocking sockets
* Graceful handling of client disconnects
* Username registration
* Message broadcasting to all connected clients
* Signal-safe (`SIGPIPE` ignored)
* Designed for Linux

---

## 🧠 Why epoll instead of select?

| select()                 | epoll()                      |
| ------------------------ | ---------------------------- |
| O(n) scan every call     | O(1) event notification      |
| fd limit (FD_SETSIZE)    | Virtually unlimited          |
| Copies fd sets each time | Kernel-managed interest list |
| Poor scalability         | Excellent for many clients   |

This makes `epoll` the **industry-standard** choice for high-performance servers.

---

## 🗂️ Project Structure

```
.
├── server.cpp
├── server-header.hpp
├── client.cpp
├── USEFULL-HEADERS/
│   ├── input.hpp
│   └── terminal.hpp
├── Makefile
└── README.md
```

---

## ⚙️ Build

### Requirements

* Linux (epoll is Linux-specific)
* g++ (C++17 or newer)

### Compile

```bash
g++ -std=c++17 server.cpp -o server
g++ -std=c++17 client.cpp -o client
```

Or simply:

```bash
make
```

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
5. Broadcast messages
6. Remove disconnected clients

### Key syscalls used

* `epoll_create1()`
* `epoll_ctl()`
* `epoll_wait()`
* `fcntl()` (non-blocking mode)
* `accept()` / `recv()` / `send()`

---

## 🔄 Event Flow

* **Server socket ready** → Accept new client
* **Client socket ready** → Read incoming message
* **Client disconnects** → Remove fd from epoll + close socket

---

## 🛡️ Safety & Stability

* `SIGPIPE` is ignored to prevent crashes on write to closed sockets
* All sockets are non-blocking
* epoll events are centrally managed

---

## 🧪 Tested On

* Ubuntu / Debian-based distros
* Kernel 5.x+

---

## 📌 Notes

* This is a learning-focused project meant to explore low-level networking
* Not intended for production use without further hardening (TLS, auth, etc.)

---

## 📜 License

MIT License

---

## 👤 Author

Single-heo
Refactor to epoll-based architecture

---

If you want a **threaded version**, **edge-triggered epoll**, or **epoll + thread pool**, feel free to extend this project 🚀
