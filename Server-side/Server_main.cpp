#include <csignal>       // sig_atomic_t (volatile signal-safe flag type)
#include <cstdlib>       // exit(), EXIT_FAILURE
#include <iostream>      // std::cout, std::cerr
#include <ifaddrs.h>     // getifaddrs(), freeifaddrs() — enumerate network interfaces
#include <arpa/inet.h>   // inet_ntop() — convert binary IP to string
#include <cstring>       // memset(), strerror()

#include "server-header.hpp"

// volatile: prevents the compiler from optimizing away reads of this variable,
// since it can be modified asynchronously by a signal handler.
// sig_atomic_t: guarantees the read/write is atomic from a signal context.
// (not currently wired to a signal handler, but good practice for future use)
volatile sig_atomic_t server_running = 1;

// Forward declaration — defined below main()
bool isLocalIP(const std::string& ip);

int main()
{
    // Prompt the operator for which local IP to bind the server to.
    // getString() with StringType::IPV4 rejects anything that isn't a
    // valid dotted-decimal IPv4 address before returning.
    std::string ip_addr = getString("Type the ip address: ", true, false, StringType::IPV4);

    // Prompt for port number; getInt enforces the valid TCP port range [0, 65535].
    int port = getInt("Type the port: ", 0, 65535);

    // Security check: refuse to bind to an IP that doesn't belong to this machine.
    // "0.0.0.0" is the wildcard address (bind on all interfaces), so it's always allowed.
    // Binding to a foreign IP would fail at bind() anyway, but this gives a clearer error.
    if (!isLocalIP(ip_addr) && ip_addr != "0.0.0.0") {
        std::cerr << "Error: IP is not assigned to this machine.\n";
        return 1;
    }

    // Construct the server: internally calls socket(), setsockopt(), bind(), listen()
    TcpServer server(port, ip_addr.c_str());

    std::cout << "Server starting...\n";

    // Enter the epoll event loop — blocks here until SERVER_IS_RUNNING becomes false
    server.run();

    return 0;
}

// ---------------------------------------------------------------------------
// isLocalIP: checks whether the given IP string is assigned to any local
// network interface on this machine.
// ---------------------------------------------------------------------------
bool isLocalIP(const std::string& ip)
{
    struct ifaddrs *ifaddr, *ifa;

    // getifaddrs() allocates and fills a linked list of all network interfaces.
    // Each node contains the interface name, address family, and address.
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return false;
    }

    bool found = false;

    // Walk the linked list; ifa->ifa_next is nullptr at the end
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        // Some interfaces (e.g. loopback aliases) may have no address — skip them
        if (!ifa->ifa_addr) continue;

        // We only care about IPv4 addresses (AF_INET).
        // AF_INET6 would be needed for IPv6 support.
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            char addr[INET_ADDRSTRLEN]; // INET_ADDRSTRLEN = 16 (enough for "255.255.255.255\0")

            // Cast the generic sockaddr* to sockaddr_in* to access sin_addr,
            // then use inet_ntop to convert the binary address to a human-readable string.
            // inet_ntop is safer than the older inet_ntoa (thread-safe, no static buffer).
            void* in_addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, in_addr, addr, INET_ADDRSTRLEN);

            // Compare the interface's string IP against what the user typed
            if (ip == addr) {
                found = true;
                break; // no need to keep iterating
            }
        }
    }

    // freeifaddrs() releases the linked list allocated by getifaddrs() —
    // must always be called to avoid a memory leak
    freeifaddrs(ifaddr);
    return found;
}