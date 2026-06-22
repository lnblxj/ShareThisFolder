#include "share_this_folder/net/upnp_client.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

static const char* SSDP_ADDR = "239.255.255.250";
static const int SSDP_PORT = 1900;

struct ParsedUrl {
    std::string host;
    int port = 80;
    std::string path = "/";
};

struct UpnpDevice {
    std::string controlUrl;
    std::string serviceType;
    std::string host;
    int port = 80;
    std::string descriptionPath;
};

static std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string trim(const std::string& s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) first++;
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) last--;
    return s.substr(first, last - first);
}

static bool parseHttpUrl(const std::string& input, ParsedUrl& out) {
    std::string url = trim(input);
    if (lowerCopy(url.substr(0, 7)) == "http://") url = url.substr(7);
    if (url.empty()) return false;

    size_t slashPos = url.find('/');
    std::string hostPort = (slashPos == std::string::npos) ? url : url.substr(0, slashPos);
    out.path = (slashPos == std::string::npos) ? "/" : url.substr(slashPos);
    if (hostPort.empty()) return false;

    size_t colonPos = hostPort.rfind(':');
    out.host = (colonPos == std::string::npos) ? hostPort : hostPort.substr(0, colonPos);
    out.port = 80;
    if (colonPos != std::string::npos) {
        try {
            out.port = std::stoi(hostPort.substr(colonPos + 1));
        } catch (...) {
            return false;
        }
    }
    return !out.host.empty();
}

static std::string resolveControlPath(const std::string& controlUrl, const std::string& descriptionPath) {
    std::string path = trim(controlUrl);
    if (path.empty()) return "";
    if (lowerCopy(path.substr(0, 7)) == "http://") {
        ParsedUrl parsed;
        return parseHttpUrl(path, parsed) ? parsed.path : "";
    }
    if (path[0] == '/') return path;

    size_t slash = descriptionPath.rfind('/');
    std::string base = (slash == std::string::npos) ? "/" : descriptionPath.substr(0, slash + 1);
    return base + path;
}

static std::string getHeaderValue(const std::string& response, const std::string& name) {
    std::string wanted = lowerCopy(name);
    size_t lineStart = 0;
    while (lineStart < response.size()) {
        size_t lineEnd = response.find("\r\n", lineStart);
        if (lineEnd == std::string::npos) lineEnd = response.size();
        std::string line = response.substr(lineStart, lineEnd - lineStart);
        size_t colon = line.find(':');
        if (colon != std::string::npos && lowerCopy(trim(line.substr(0, colon))) == wanted)
            return trim(line.substr(colon + 1));
        lineStart = lineEnd + 2;
    }
    return "";
}

static std::string extractTag(const std::string& xml, const std::string& tag) {
    std::string lowerXml = lowerCopy(xml);
    std::string openTag = "<" + lowerCopy(tag);
    std::string closeTag = "</" + lowerCopy(tag) + ">";

    size_t open = lowerXml.find(openTag);
    while (open != std::string::npos) {
        size_t openEnd = lowerXml.find('>', open);
        if (openEnd == std::string::npos) return "";
        size_t close = lowerXml.find(closeTag, openEnd + 1);
        if (close == std::string::npos) return "";
        return xml.substr(openEnd + 1, close - openEnd - 1);
    }
    return "";
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

static bool resolveHost(const std::string& host, int port, sockaddr_in& addr) {
    addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));

    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

static std::string httpRequest(const std::string& host, int port, const std::string& request) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in addr = {};
    if (!resolveHost(host, port, addr)) {
        closesocket(sock);
        return "";
    }

    int timeout = 4000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        return "";
    }

    send(sock, request.c_str(), static_cast<int>(request.size()), 0);

    std::string response;
    char buf[4096];
    int n = 0;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
        response.append(buf, n);

    closesocket(sock);
    return response;
}

static std::string httpGet(const std::string& host, int port, const std::string& path) {
    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + std::to_string(port) + "\r\n"
        "Connection: close\r\n"
        "\r\n";
    return httpRequest(host, port, req);
}

static std::vector<std::string> discoverLocations() {
    const char* searchTargets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
        "upnp:rootdevice"
    };

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return {};

    int timeout = 1200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_ADDR, &dest.sin_addr);

    for (const char* st : searchTargets) {
        std::string req =
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\n"
            "ST: " + std::string(st) + "\r\n"
            "\r\n";
        sendto(sock, req.c_str(), static_cast<int>(req.size()), 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    }

    std::set<std::string> seen;
    std::vector<std::string> locations;
    for (;;) {
        char buf[4096];
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;

        std::string response(buf, n);
        std::string location = getHeaderValue(response, "location");
        if (!location.empty() && seen.insert(location).second)
            locations.push_back(location);
    }

    closesocket(sock);
    return locations;
}

static bool parseDeviceDescription(const ParsedUrl& descUrl, const std::string& desc, UpnpDevice& dev) {
    std::string lowerDesc = lowerCopy(desc);
    size_t pos = 0;
    while ((pos = lowerDesc.find("<service", pos)) != std::string::npos) {
        size_t openEnd = lowerDesc.find('>', pos);
        if (openEnd == std::string::npos) break;
        size_t end = lowerDesc.find("</service>", openEnd);
        if (end == std::string::npos) break;

        std::string block = desc.substr(openEnd + 1, end - openEnd - 1);
        std::string serviceType = trim(extractTag(block, "serviceType"));
        std::string lowerService = lowerCopy(serviceType);
        if (lowerService.find("wanipconnection") != std::string::npos ||
            lowerService.find("wanpppconnection") != std::string::npos) {
            std::string controlRaw = trim(extractTag(block, "controlURL"));
            ParsedUrl controlUrl;
            bool absoluteControlUrl = lowerCopy(controlRaw.substr(0, 7)) == "http://" &&
                                      parseHttpUrl(controlRaw, controlUrl);
            std::string control = absoluteControlUrl ? controlUrl.path : resolveControlPath(controlRaw, descUrl.path);
            if (control.empty()) return false;

            dev.host = absoluteControlUrl ? controlUrl.host : descUrl.host;
            dev.port = absoluteControlUrl ? controlUrl.port : descUrl.port;
            dev.descriptionPath = descUrl.path;
            dev.serviceType = serviceType;
            dev.controlUrl = control;
            return true;
        }

        pos = end + 10;
    }
    return false;
}

static UpnpDevice discoverDevice() {
    for (const std::string& location : discoverLocations()) {
        ParsedUrl descUrl;
        if (!parseHttpUrl(location, descUrl)) continue;

        std::string desc = httpGet(descUrl.host, descUrl.port, descUrl.path);
        if (desc.empty()) continue;

        UpnpDevice dev;
        if (parseDeviceDescription(descUrl, desc, dev))
            return dev;
    }
    return {};
}

static std::string localAddressFor(const std::string& remoteHost, int remotePort) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in remote = {};
    if (!resolveHost(remoteHost, remotePort, remote)) {
        closesocket(sock);
        return "";
    }

    connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote));

    sockaddr_in local = {};
    int len = sizeof(local);
    std::string result;
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
        result = ip;
    }
    closesocket(sock);
    return result;
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

    return httpRequest(dev.host, dev.port, req);
}

static bool soapSucceeded(const std::string& response) {
    std::string lower = lowerCopy(response);
    if (lower.find("500 internal") != std::string::npos || lower.find("<fault") != std::string::npos)
        return false;
    return lower.find(" 200 ") != std::string::npos ||
           lower.find("http/1.1 200") != std::string::npos ||
           lower.find("http/1.0 200") != std::string::npos;
}

static bool addPortMapping(const UpnpDevice& dev, uint16_t externalPort, uint16_t internalPort,
                           const std::string& internalIp, const std::string& description) {
    std::string body =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>"
        "<NewInternalPort>" + std::to_string(internalPort) + "</NewInternalPort>"
        "<NewInternalClient>" + xmlEscape(internalIp) + "</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>" + xmlEscape(description) + "</NewPortMappingDescription>"
        "<NewLeaseDuration>0</NewLeaseDuration>";

    return soapSucceeded(soapRequest(dev, "AddPortMapping", body));
}

static uint16_t addAnyPortMapping(const UpnpDevice& dev, uint16_t preferredExternalPort, uint16_t internalPort,
                                  const std::string& internalIp, const std::string& description) {
    if (lowerCopy(dev.serviceType).find(":2") == std::string::npos)
        return 0;

    std::string body =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(preferredExternalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>"
        "<NewInternalPort>" + std::to_string(internalPort) + "</NewInternalPort>"
        "<NewInternalClient>" + xmlEscape(internalIp) + "</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>" + xmlEscape(description) + "</NewPortMappingDescription>"
        "<NewLeaseDuration>0</NewLeaseDuration>";

    std::string resp = soapRequest(dev, "AddAnyPortMapping", body);
    if (!soapSucceeded(resp)) return 0;

    std::string reserved = trim(extractTag(resp, "NewReservedPort"));
    if (reserved.empty()) return preferredExternalPort;
    try {
        int p = std::stoi(reserved);
        return (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 0;
    } catch (...) {
        return 0;
    }
}

std::string upnpGetExternalIP() {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return "";

    std::string resp = soapRequest(dev, "GetExternalIPAddress", "");
    return trim(extractTag(resp, "NewExternalIPAddress"));
}

UpnpMapping upnpMapTcpPort(uint16_t preferredExternalPort, uint16_t internalPort,
                           const std::string& internalIp, const std::string& description) {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return {"", 0, false};

    std::vector<std::string> internalIps;
    if (!internalIp.empty()) internalIps.push_back(internalIp);
    std::string routedIp = localAddressFor(dev.host, dev.port);
    if (!routedIp.empty() && std::find(internalIps.begin(), internalIps.end(), routedIp) == internalIps.end())
        internalIps.push_back(routedIp);

    for (const std::string& ip : internalIps) {
        if (addPortMapping(dev, preferredExternalPort, internalPort, ip, description)) {
            std::string externalIp = trim(extractTag(soapRequest(dev, "GetExternalIPAddress", ""), "NewExternalIPAddress"));
            return {externalIp, preferredExternalPort, !externalIp.empty()};
        }

        uint16_t anyPort = addAnyPortMapping(dev, preferredExternalPort, internalPort, ip, description);
        if (anyPort != 0) {
            std::string externalIp = trim(extractTag(soapRequest(dev, "GetExternalIPAddress", ""), "NewExternalIPAddress"));
            return {externalIp, anyPort, !externalIp.empty()};
        }
    }

    return {"", 0, false};
}

bool upnpAddPortMapping(uint16_t externalPort, uint16_t internalPort, const std::string& internalIp, const std::string& description) {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return false;

    std::vector<std::string> internalIps;
    if (!internalIp.empty()) internalIps.push_back(internalIp);
    std::string routedIp = localAddressFor(dev.host, dev.port);
    if (!routedIp.empty() && std::find(internalIps.begin(), internalIps.end(), routedIp) == internalIps.end())
        internalIps.push_back(routedIp);

    for (const std::string& ip : internalIps) {
        if (addPortMapping(dev, externalPort, internalPort, ip, description))
            return true;
    }

    return false;
}

bool upnpRemovePortMapping(uint16_t externalPort) {
    UpnpDevice dev = discoverDevice();
    if (dev.controlUrl.empty()) return false;

    std::string body =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>";

    return soapSucceeded(soapRequest(dev, "DeletePortMapping", body));
}
