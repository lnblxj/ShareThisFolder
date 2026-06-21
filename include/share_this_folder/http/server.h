#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <filesystem>

class HttpServer {
public:
    HttpServer(const std::filesystem::path& rootDir, int port, const std::string& bindAddr = "0.0.0.0");
    ~HttpServer();

    bool start();
    void stop();
    int  getPort() const;

private:
    void run();
    void handleClient(intptr_t clientSock);

    std::string buildDirListing(const std::filesystem::path& diskPath, const std::string& urlPath);
    std::string getMimeType(const std::string& ext);
    std::string urlDecode(const std::string& s);
    std::string formatSize(uint64_t bytes);

    std::filesystem::path rootDir_;
    std::string  bindAddr_;
    int         port_;
    intptr_t    listenSock_;
    std::atomic<bool> running_;
    std::thread       thread_;
};
