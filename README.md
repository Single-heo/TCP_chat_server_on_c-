# TCP Chat Application

A multi-client TCP chat application written in **C++17** for Linux using POSIX sockets and the Linux `epoll` API.

The project was developed as a learning exercise in systems programming and modern network programming. It implements secure user authentication using **Argon2id** (libsodium), an event-driven server architecture, and provides installation scripts that automatically configure the application as a native Linux service.

---

# Features

## Networking

* Multi-client TCP server
* Event-driven architecture using `epoll`
* Edge-triggered I/O
* Non-blocking sockets
* Real-time message broadcasting
* Graceful client disconnection
* Partial TCP message reassembly

## Authentication

* User registration
* User login
* Argon2id password hashing via libsodium
* Constant-time password verification
* JSON credential database
* Username and password validation

## Client

* Interactive terminal interface
* Non-blocking keyboard input
* Raw terminal mode
* Local slash-command processing
* Graceful disconnect

## Server

* Single-threaded event loop
* Local IP validation before binding
* Authentication required before chatting
* Connection limit enforcement
* Username session tracking
* `SIGPIPE` protection during socket writes

## Installation

The project includes an automated installer that:

* Detects the Linux package manager
* Installs all required dependencies
* Compiles the project
* Creates a dedicated `tcpserver` system user
* Creates configuration, database and log directories
* Installs the executables
* Installs and enables the systemd service
* Starts the server automatically

No manual dependency installation or CMake commands are required for normal installation.

---

# Project Structure

```text
.
├── Client-side/
├── Server-side/
├── common/
│   ├── Logger/
│   ├── config/
│   ├── service/
│   ├── simpleini/
│   ├── nlohmann/
│   └── ...
├── DataBase/
├── install.sh
├── uninstall.sh
├── CMakeLists.txt
└── README.md
```

---

# Requirements

* Linux
* systemd
* CMake 3.16 or newer
* C++17 compatible compiler

The installer automatically installs all remaining dependencies.

---

# Quick Installation

Clone the repository:

```bash
git clone https://github.com/Single-heo/TCP_chat_server_on_c-.git
cd TCP_chat_server_on_c-
```

Choose what to install.

## Install both server and client

```bash
./install.sh both
```

## Install only the server

```bash
./install.sh server
```

## Install only the client

```bash
./install.sh client
```

The installer automatically:

* installs dependencies
* builds the project
* installs the binaries
* creates the service user
* installs configuration files
* installs the database
* enables the systemd service (server installation)
* starts the server

---

# Installed Files

| Path                                    | Purpose             |
| --------------------------------------- | ------------------- |
| `/usr/bin/tcpserver/server`             | Server executable   |
| `/usr/bin/tcpserver/client`             | Client executable   |
| `/etc/tcpserver/`                       | Configuration files |
| `/var/lib/tcpserver/`                   | Credential database |
| `/var/log/tcpserver/`                   | Log files           |
| `/etc/systemd/system/tcpserver.service` | systemd service     |

---

# Running

## Server

When installed with:

```bash
./install.sh server
```

or

```bash
./install.sh both
```

the server is installed as a native systemd service and starts automatically.

Useful commands:

```bash
systemctl status tcpserver
systemctl restart tcpserver
systemctl stop tcpserver
journalctl -u tcpserver -f
```

---

## Client

Run the client with:

```bash
/usr/bin/tcpserver/client
```

Enter the server IP and port when prompted.

---

# Client Commands

| Command                            | Description             |
| ---------------------------------- | ----------------------- |
| `/register <username>\|<password>` | Create an account       |
| `/login <username>\|<password>`    | Authenticate            |
| `/help`                            | Show available commands |
| `/clear`                           | Clear the terminal      |
| `/exit`                            | Disconnect              |

---

# Manual Build (Developers)

The installer is the recommended installation method.

If you only want to compile the project:

```bash
cmake -S . -B build
cmake --build build
```

or choose individual targets:

```bash
cmake -S . -B build \
    -DBUILD_SERVER=ON \
    -DBUILD_CLIENT=OFF
```

---

# Password Security

Passwords are never stored in plaintext.

Authentication uses **Argon2id** through libsodium.

Each password hash contains:

* random salt
* memory cost parameters
* time cost parameters

Password verification is performed using libsodium's constant-time verification API.

---

# Architecture

## Server

* Single-threaded `epoll` event loop
* Non-blocking sockets
* Per-client connection state
* Authentication before messaging
* Broadcast-based chat model

## Client

* `select()` monitors:

  * keyboard input
  * server socket
* Raw terminal mode
* Non-blocking stdin
* Local command dispatcher

---

# Uninstall

Remove the installed binaries and stop the service:

```bash
./uninstall.sh
```

This removes:

* installed executables
* systemd service

while preserving:

* configuration
* credential database
* log files
* `tcpserver` service user

---

## Complete Removal

To remove everything, including configuration, logs, database and the dedicated service account:

```bash
./uninstall.sh --purge
```

---

# Current Limitations

* No TLS/SSL encryption
* Fixed maximum number of simultaneous clients
* Single-threaded architecture
* No message history
* No offline messaging
* No write buffering for `EAGAIN`
* Receive path can be improved by reading until `EAGAIN` in edge-triggered mode

---

# Planned Features

* TLS support
* Private messaging
* Chat rooms
* Message history
* Configurable server settings
* Improved send buffering
* Unit tests

---

# Learning Objectives

This project was developed to gain practical experience with:

* Understand system engineering
* Professional Deploy as production services
* POSIX sockets
* TCP networking
* Linux `epoll`
* Event-driven programming
* Non-blocking I/O
* `termios`
* Modern C++
* CMake
* systemd services
* Linux permissions and ACLs
* Secure password storage with Argon2id

---

# License

This project is open source and intended primarily for educational purposes.

---

# Author

**Heitor Zanin Mello**

Learning project focused on Linux systems programming, networking, secure authentication, and service-oriented application development.
