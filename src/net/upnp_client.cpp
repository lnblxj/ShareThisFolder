#include "share_this_folder/net/upnp_client.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

static const char* SSDP_ADDR = "239.255.255.250";
static const int SSDP_PORT = 1900;
static const char* SSDP_MSEARCH =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 3\r\n"
    "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
    "\r\n";

static std::string httpRequest(const std::string& host, int port, const std::string& path) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));

    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        closesocket(sock); return "";
    }
    addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    int timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock); return "";
    }

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    send(sock, req.c_str(), static_cast<int>(req.size()), 0);

    std::string response;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    closesocket(sock);
    return response;
}

static std::string extractTag(const std::string& xml, const std::string& tag) {
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";
    size_t start = xml.find(openTag);
    if (start == std::string::npos) return "";
    start += openTag.size();
    size_t end = xml.find(closeTag, start);
    if (end == std::string::npos) return "";
    return xml.substr(start, end - start);
}

static std::string xmlEscape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

struct UpnpDevice {
    std::string controlUrl;
    std::string serviceType;
    std::string host;
    int port;
};

static UpnpDevice discoverDevice() {
    UpnpDevice dev = {};

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return dev;

    int timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_ADDR, &dest.sin_addr);

    sendto(sock, SSDP_MSEARCH, static_cast<int>(strlen(SSDP_MSEARCH)), 0, (sockaddr*)&dest, sizeof(dest));

    char buf[4096];
    sockaddr_in from = {};
    int fromLen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
    closesocket(sock);

    if (n <= 0) return dev;
    buf[n] = '\0';

    std::string response(buf, n);

    // Extract LOCATION header
    size_t locPos = response.find("LOCATION:");
    if (locPos == std::string::npos) return dev;
    locPos += 9;
    while (locPos < response.size() && response[locPos] == ' ') locPos++;
    size_t locEnd = response.find("\r\n", locPos);
    if (locEnd == std::string::npos) return dev;
    std::string location = response.substr(locPos, locEnd - locPos);

    // Parse URL: http://host:port/path
    std::string url = location;
    if (url.substr(0, 7) == "http://") url = url.substr(7);
    size_t slashPos = url.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? url.substr(0, slashPos) : url;
    std::string path = (slashPos != std::string::npos) ? url.substr(slashPos) : "/";

    size_t colonPos = hostPort.find(':');
    dev.host = hostPort.substr(0, colonPos);
    dev.port = (colonPos != std::string::npos) ? std::stoi(hostPort.substr(colonPos + 1)) : 80;

    // Fetch device description
    std::string desc = httpRequest(dev.host, dev.port, path);
    if (desc.empty()) return dev;

    // Find WANIPConnection service
    size_t svcPos = desc.find("WANIPConnection");
    if (svcPos == std::string::npos) {
        svcPos = desc.find("WANPPPConnection");
        if (svcPos == std::string::npos) return dev;
        dev.serviceType = "urn:schemas-upnp-org:service:WANPPPConnection:1";
    } else {
        dev.serviceType = "urn:schemas-upnp-org:service:WANIPConnection:1";
    }

    // Extract controlURL
    size_t ctrlStart = desc.rfind("<controlURL>", 0, svcPos);
    if (ctrlStart == std::string::npos) {
        ctrlStart = desc.find("<controlURL>", svcPos);
    }
    if (ctrlStart != std::string::npos) {
        ctrlStart += 13;
        size_t ctrlEnd = desc.find("</controlURL>", ctrlStart);
        if (ctrlEnd != std::string::npos) {
            dev.controlUrl = desc.substr(ctrlStart, ctrlEnd - ctrlStart);
        }
    }

    return dev;
}

static std::string soapRequest(const UpnpDevice& dev, const std::string& action, const std::string& body) {
    std::string soapBody =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:" + action + " xmlns:u=\"" + dev.serviceType + "\">\r\n"
        + body +
        "</u:" + action + ">\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>\r\n";

    std::string req =
        "POST " + dev.controlUrl + " HTTP/1.1\r\n"
        "Host: " + dev.host + ":" + std::to_string(dev.port) + "\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"" + dev.serviceType + "#" + action + "\"\r\n"
        "Content-Length: " + std::to_string(soapBody.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + soapBody;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(dev.port));

    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(dev.host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        closesocket(sock); return "";
    }
    addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    int timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock); return "";
    }

    send(sock, req.c_str(), static_cast<int>(req.size()), 0);

    std::string response;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    closesocket(sock);
    return response;
}

std::string upnpGetExternalIP() {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return "";

    std::string resp = soapRequest(dev, "GetExternalIPAddress", "");
    return extractTag(resp, "NewExternalIPAddress");
}

bool upnpAddPortMapping(uint16_t externalPort, uint16_t internalPort, const std::string& internalIp, const std::string& description) {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return false;

    std::string body =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>"
        "<NewInternalPort>" + std::to_string(internalPort) + "</NewInternalPort>"
        "<NewInternalClient>" + xmlEscape(internalIp) + "</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>" + xmlEscape(description) + "</NewPortMappingDescription>"
        "<NewLeaseDuration>0</NewLeaseDuration>";

    std::string resp = soapRequest(dev, "AddPortMapping", body);
    return resp.find("200 OK") != std::string::npos || resp.find("s:Body") != std::string::npos;
}

bool upnpRemovePortMapping(uint16_t externalPort) {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return false;

    std::string body =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>";

    std::string resp = soapRequest(dev, "DeletePortMapping", body);
    return resp.find("200 OK") != std::string::npos || resp.find("s:Body") != std::string::npos;
}
