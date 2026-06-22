#include "share_this_folder/net/stun_client.h"
#include "share_this_folder/net/upnp_client.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <random>
#include <thread>
#include <atomic>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

static std::atomic<bool> g_active{false};
static SOCKET g_keepSock = INVALID_SOCKET;
static SOCKET g_fwdListenSock = INVALID_SOCKET;
static uint16_t g_targetPort = 0;
static uint16_t g_upnpTunnelPort = 0;

static uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static void makeStunReq(uint8_t* out, int* len) {
    out[0]=0x00; out[1]=0x01; out[2]=0x00; out[3]=0x00;
    out[4]=0x21; out[5]=0x12; out[6]=0xA4; out[7]=0x42;
    std::random_device rd; std::mt19937 g(rd());
    std::uniform_int_distribution<int> d(0,255);
    for (int i=0;i<12;i++) out[8+i]=(uint8_t)d(g);
    *len=20;
}

static bool parseStun(const uint8_t* buf, int n, std::string& ip, uint16_t& port) {
    if (n<20||readU16BE(buf)!=0x0101) return false;
    uint16_t rlen=readU16BE(buf+2);
    if (rlen>n-20) return false;
    const uint8_t mc[4]={0x21,0x12,0xA4,0x42};
    int off=20;
    while(off+4<=20+rlen){
        uint16_t at=readU16BE(buf+off), al=readU16BE(buf+off+2);
        if(at==0x0020&&al>=8){const uint8_t* v=buf+off+4;
            if(v[1]==1){uint16_t xp=readU16BE(v+2)^readU16BE(mc);
            uint8_t xa[4];for(int i=0;i<4;i++)xa[i]=v[4+i]^mc[i];
            in_addr ia;memcpy(&ia,xa,4);char s[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,&ia,s,sizeof(s));ip=s;port=xp;return true;}}
        else if(at==0x0001&&al>=8){const uint8_t* v=buf+off+4;
            if(v[1]==1){in_addr ia;memcpy(&ia,v+4,4);char s[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,&ia,s,sizeof(s));ip=s;port=readU16BE(v+2);return true;}}
        off+=4+((al+3)&~3);
    }
    return false;
}

static bool resolve(const std::string& h, uint16_t p, sockaddr_in& a) {
    addrinfo hi={},*r=nullptr;hi.ai_family=AF_INET;
    if(getaddrinfo(h.c_str(),nullptr,&hi,&r)!=0||!r)return false;
    a.sin_family=AF_INET;a.sin_port=htons(p);
    a.sin_addr=((sockaddr_in*)r->ai_addr)->sin_addr;
    freeaddrinfo(r);return true;
}

static std::string localAddressForRemote(const std::string& h, uint16_t p) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in ra = {};
    if (!resolve(h, p, ra)) { closesocket(s); return ""; }
    connect(s, (sockaddr*)&ra, sizeof(ra));
    sockaddr_in la = {}; int ll = sizeof(la);
    std::string result;
    if (getsockname(s, (sockaddr*)&la, &ll) == 0) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &la.sin_addr, ip, sizeof(ip));
        result = ip;
    }
    closesocket(s);
    return result;
}

static uint16_t reserveLocalTcpPort(const std::string& localIp) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = 0;
    inet_pton(AF_INET, localIp.c_str(), &la.sin_addr);
    if (bind(s, (sockaddr*)&la, sizeof(la)) != 0) {
        closesocket(s);
        return 0;
    }

    int ll = sizeof(la);
    uint16_t port = 0;
    if (getsockname(s, (sockaddr*)&la, &ll) == 0)
        port = ntohs(la.sin_port);
    closesocket(s);
    return port;
}

static SOCKET tcpConnect(const std::string& h, uint16_t p, const std::string& localIp = "", uint16_t localPort = 0) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(s==INVALID_SOCKET)return INVALID_SOCKET;
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    if (localPort != 0 && !localIp.empty()) {
        sockaddr_in la = {};
        la.sin_family = AF_INET;
        la.sin_port = htons(localPort);
        inet_pton(AF_INET, localIp.c_str(), &la.sin_addr);
        if (bind(s, (sockaddr*)&la, sizeof(la)) != 0) { closesocket(s); return INVALID_SOCKET; }
    }
    sockaddr_in ra={};
    if(!resolve(h,p,ra)){closesocket(s);return INVALID_SOCKET;}
    u_long nb=1;ioctlsocket(s,FIONBIO,&nb);
    connect(s,(sockaddr*)&ra,sizeof(ra));
    fd_set ws;FD_ZERO(&ws);FD_SET(s,&ws);
    timeval tv={5,0};select(0,nullptr,&ws,nullptr,&tv);
    int se=0,sl=sizeof(se);getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&se,&sl);
    if(se!=0){closesocket(s);return INVALID_SOCKET;}
    u_long b=0;ioctlsocket(s,FIONBIO,&b);
    return s;
}

// Forward data between two sockets
static void forwardData(SOCKET src, SOCKET dst) {
    char buf[65536];
    while (g_active) {
        fd_set rset; FD_ZERO(&rset); FD_SET(src, &rset);
        timeval tv = {1, 0};
        int sel = select(0, &rset, nullptr, nullptr, &tv);
        if (sel <= 0) { if (!g_active) break; continue; }
        int n = recv(src, buf, sizeof(buf), 0);
        if (n <= 0) break;
        int sent = 0;
        while (sent < n) {
            int s = send(dst, buf + sent, n - sent, 0);
            if (s <= 0) return;
            sent += s;
        }
    }
}

// Handle one forwarded connection
static void handleForward(SOCKET clientSock) {
    SOCKET targetSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (targetSock == INVALID_SOCKET) { closesocket(clientSock); return; }

    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_port = htons(g_targetPort);
    target.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

    if (connect(targetSock, (sockaddr*)&target, sizeof(target)) != 0) {
        closesocket(clientSock);
        closesocket(targetSock);
        return;
    }

    std::thread([clientSock, targetSock]() {
        forwardData(clientSock, targetSock);
        closesocket(targetSock);
    }).detach();

    forwardData(targetSock, clientSock);
    closesocket(clientSock);
}

// Forwarding server: listen on tunnel port, forward to HTTP server
// MUST bind to specific local IP (not 0.0.0.0) on Windows to coexist with keep-alive socket
static void forwardServerLoop(uint16_t listenPort, std::string localIp) {
    g_fwdListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_fwdListenSock == INVALID_SOCKET) return;

    int reuse = 1;
    setsockopt(g_fwdListenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listenPort);
    inet_pton(AF_INET, localIp.c_str(), &addr.sin_addr);

    if (bind(g_fwdListenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(g_fwdListenSock);
        g_fwdListenSock = INVALID_SOCKET;
        return;
    }

    listen(g_fwdListenSock, SOMAXCONN);

    while (g_active) {
        fd_set rset; FD_ZERO(&rset); FD_SET(g_fwdListenSock, &rset);
        timeval tv = {1, 0};
        int sel = select(0, &rset, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET client = accept(g_fwdListenSock, (sockaddr*)&clientAddr, &addrLen);
        if (client == INVALID_SOCKET) continue;

        std::thread(handleForward, client).detach();
    }

    closesocket(g_fwdListenSock);
    g_fwdListenSock = INVALID_SOCKET;
}

// Keep-alive loop
static void keepAliveLoop(std::string host, uint16_t port) {
    std::string req = "HEAD / HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nConnection: keep-alive\r\n\r\n";
    char buf[4096];
    while (g_active) {
        Sleep(15000);
        if (g_keepSock == INVALID_SOCKET) {
            g_keepSock = tcpConnect(host, port);
            if (g_keepSock == INVALID_SOCKET) continue;
        }
        if (send(g_keepSock, req.c_str(), (int)req.size(), 0) <= 0) {
            closesocket(g_keepSock);
            g_keepSock = tcpConnect(host, port);
            continue;
        }
        if (recv(g_keepSock, buf, sizeof(buf), 0) <= 0) {
            closesocket(g_keepSock);
            g_keepSock = tcpConnect(host, port);
        }
    }
}

bool tunnelStart(const std::string& httpHost, uint16_t httpPort,
                 const std::string& stunHost, uint16_t stunPort,
                 uint16_t bindPort, const std::string& bindAddr,
                 StunResult& result) {
    result = {"", 0, false};
    g_targetPort = bindPort;

    // Step 1: TCP keep-alive connection (creates NAT mapping)
    std::string plannedLocalIp = bindAddr != "0.0.0.0" ? bindAddr : localAddressForRemote(httpHost, httpPort);
    uint16_t plannedTunnelPort = plannedLocalIp.empty() ? 0 : reserveLocalTcpPort(plannedLocalIp);
    if (plannedTunnelPort != 0 &&
        upnpAddPortMapping(plannedTunnelPort, plannedTunnelPort, plannedLocalIp, "ShareThisFolderTunnel")) {
        g_upnpTunnelPort = plannedTunnelPort;
        g_keepSock = tcpConnect(httpHost, httpPort, plannedLocalIp, plannedTunnelPort);
        if (g_keepSock == INVALID_SOCKET) {
            upnpRemovePortMapping(g_upnpTunnelPort);
            g_upnpTunnelPort = 0;
        }
    }

    if (g_keepSock == INVALID_SOCKET)
        g_keepSock = tcpConnect(httpHost, httpPort);
    if (g_keepSock == INVALID_SOCKET) {
        std::cerr << "  [Tunnel] Cannot connect to " << httpHost << ":" << httpPort << std::endl;
        return false;
    }

    sockaddr_in la = {}; int ll = sizeof(la);
    getsockname(g_keepSock, (sockaddr*)&la, &ll);
    uint16_t tunnelPort = ntohs(la.sin_port);
    char localIpStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &la.sin_addr, localIpStr, sizeof(localIpStr));
    std::string localIp(localIpStr);

    // In double-NAT/CGNAT setups, STUN gives the ISP NAT public endpoint.
    // UPnP is only used to open the first-hop router port back to this host.
    if (g_upnpTunnelPort == 0 && upnpAddPortMapping(tunnelPort, tunnelPort, localIp, "ShareThisFolderTunnel"))
        g_upnpTunnelPort = tunnelPort;

    // Step 2: STUN discovery from same port
    // Try TCP STUN
    SOCKET stunSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (stunSock != INVALID_SOCKET) {
        int reuse = 1;
        setsockopt(stunSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        sockaddr_in sla = {};
        sla.sin_family = AF_INET;
        sla.sin_port = htons(tunnelPort);
        sla.sin_addr = la.sin_addr;
        if (bind(stunSock, (sockaddr*)&sla, sizeof(sla)) == 0) {
            sockaddr_in sra = {};
            if (resolve(stunHost, stunPort, sra)) {
                u_long nb = 1; ioctlsocket(stunSock, FIONBIO, &nb);
                connect(stunSock, (sockaddr*)&sra, sizeof(sra));
                fd_set ws; FD_ZERO(&ws); FD_SET(stunSock, &ws);
                timeval tv = {3, 0}; select(0, nullptr, &ws, nullptr, &tv);
                int se = 0, sl2 = sizeof(se);
                getsockopt(stunSock, SOL_SOCKET, SO_ERROR, (char*)&se, &sl2);
                if (se == 0) {
                    u_long b = 0; ioctlsocket(stunSock, FIONBIO, &b);
                    int t = 3000; setsockopt(stunSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
                    uint8_t req[20]; int rl; makeStunReq(req, &rl);
                    send(stunSock, (const char*)req, rl, 0);
                    uint8_t hdr[20];
                    int n = recv(stunSock, (char*)hdr, 20, MSG_WAITALL);
                    if (n == 20 && readU16BE(hdr) == 0x0101) {
                        uint16_t bl = readU16BE(hdr + 2);
                        if (bl <= 1024) {
                            uint8_t body[1024];
                            n = recv(stunSock, (char*)body, bl, MSG_WAITALL);
                            if (n == bl) {
                                uint8_t full[1044]; memcpy(full, hdr, 20); memcpy(full + 20, body, bl);
                                parseStun(full, 20 + bl, result.ip, result.port);
                            }
                        }
                    }
                }
            }
        }
        closesocket(stunSock);
    }

    if (result.ip.empty()) {
        closesocket(g_keepSock);
        g_keepSock = INVALID_SOCKET;
        return false;
    }

    result.success = true;
    g_active = true;

    // Step 3: Start forwarding server on tunnel port (bind to specific local IP)
    std::thread(forwardServerLoop, tunnelPort, localIp).detach();

    // Step 4: Start keep-alive loop
    std::thread(keepAliveLoop, httpHost, httpPort).detach();

    return true;
}

void tunnelStop() {
    g_active = false;
    if (g_keepSock != INVALID_SOCKET) { closesocket(g_keepSock); g_keepSock = INVALID_SOCKET; }
    if (g_fwdListenSock != INVALID_SOCKET) { closesocket(g_fwdListenSock); g_fwdListenSock = INVALID_SOCKET; }
    if (g_upnpTunnelPort != 0) { upnpRemovePortMapping(g_upnpTunnelPort); g_upnpTunnelPort = 0; }
}
