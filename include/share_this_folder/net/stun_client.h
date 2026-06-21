#pragma once
#include <string>
#include <cstdint>
#include <atomic>

struct StunResult {
    std::string ip;
    uint16_t port;
    bool success;
};

bool tunnelStart(const std::string& httpHost, uint16_t httpPort,
                 const std::string& stunHost, uint16_t stunPort,
                 uint16_t bindPort, const std::string& bindAddr,
                 StunResult& result);
void tunnelStop();
