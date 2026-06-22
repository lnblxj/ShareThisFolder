#pragma once
#include <string>
#include <cstdint>

struct UpnpMapping {
    std::string externalIp;
    uint16_t externalPort;
    bool success;
};

UpnpMapping upnpMapTcpPort(uint16_t preferredExternalPort, uint16_t internalPort,
                           const std::string& internalIp, const std::string& description);
bool upnpAddPortMapping(uint16_t externalPort, uint16_t internalPort, const std::string& internalIp, const std::string& description);
bool upnpRemovePortMapping(uint16_t externalPort);
std::string upnpGetExternalIP();
