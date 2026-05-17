#include "network.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

std::vector<std::string> getLocalIPs() {
    std::vector<std::string> ips;

    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) != 0)
        return ips;

    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0)
        return ips;

    for (auto* p = result; p; p = p->ai_next) {
        auto* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        std::string ipStr(ip);
        if (ipStr != "127.0.0.1" && !ipStr.empty())
            ips.push_back(ipStr);
    }

    freeaddrinfo(result);

    if (ips.empty()) {
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock != INVALID_SOCKET) {
            sockaddr_in remote = {};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(80);
            inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
            connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote));

            sockaddr_in local = {};
            int len = sizeof(local);
            getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len);
            closesocket(sock);

            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
            if (std::string(ip) != "0.0.0.0")
                ips.push_back(ip);
        }
    }

    return ips;
}

int findAvailablePort(int preferred) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return preferred;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(preferred));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        int len = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
        closesocket(sock);
        return ntohs(addr.sin_port);
    }

    addr.sin_port = 0;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        int len = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
        closesocket(sock);
        return ntohs(addr.sin_port);
    }

    closesocket(sock);
    return preferred;
}
