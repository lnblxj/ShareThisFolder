#pragma once
#include <string>
#include <cstdint>

bool upnpAddPortMapping(uint16_t externalPort, uint16_t internalPort, const std::string& internalIp, const std::string& description);
bool upnpRemovePortMapping(uint16_t externalPort);
std::string upnpGetExternalIP();
