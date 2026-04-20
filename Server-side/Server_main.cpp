#include <csignal>
#include <cstdlib>
#include <iostream>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <cstring>

#include "server-header.hpp"
// Global flag to control server loop
volatile sig_atomic_t server_running = 1;

bool isLocalIP(const std::string& ip);

int main() {
    std::string ip_addr = getString("Type the ip address: ", true, false, StringType::IPV4);
    int port = getInt("Type the port: ", 0, 65535);

    if (!isLocalIP(ip_addr) && ip_addr != "0.0.0.0") {
        std::cerr << "Error: IP is not assigned to this machine.\n";
        return 1;
    }

    TcpServer server(port, ip_addr.c_str());

    std::cout << "Server starting...\n";
    server.run();

    return 0;
}

bool isLocalIP(const std::string &ip)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return false;
    }

    bool found = false;

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;

        if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
            char addr[INET_ADDRSTRLEN];
            void *in_addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;

            inet_ntop(AF_INET, in_addr, addr, INET_ADDRSTRLEN);

            if (ip == addr) {
                found = true;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return found;
}
