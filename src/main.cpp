#include "i18n.h"
#include "network.h"
#include "server.h"
#include <winsock2.h>
#include <windows.h>
#include <urlmon.h>
#include <shellapi.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <csignal>
#include <cstdio>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "urlmon.lib")

static HttpServer* g_server = nullptr;

static void signalHandler(int) {
    if (g_server) g_server->stop();
}

static std::string urlEncode(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            result += static_cast<char>(c);
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

static std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &result[0], len, nullptr, nullptr);
    return result;
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SetConsoleOutputCP(65001);
    i18n::init();

    wchar_t wcwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, wcwd);
    std::filesystem::path rootDir(wcwd);
    std::string rootDirUtf8 = wideToUtf8(wcwd);

    auto ips = getLocalIPs();
    if (ips.empty()) {
        std::cerr << i18n::get("error_ip") << "\n";
        WSACleanup();
        return 1;
    }

    int port = findAvailablePort(8080);

    HttpServer server(rootDir, port);
    g_server = &server;

    if (!server.start()) {
        std::cerr << i18n::get("error_listen") << "\n";
        WSACleanup();
        return 1;
    }

    port = server.getPort();
    std::string url = "http://" + ips[0] + ":" + std::to_string(port) + "/";

    // Print server info
    std::cout << "\n"
              << "  " << i18n::get("title") << "\n"
              << "  ------------------------------------------------\n\n"
              << "  " << i18n::get("sharing_dir") << rootDirUtf8 << "\n\n"
              << "  " << i18n::get("open_url") << url << "\n\n";

    if (ips.size() > 1) {
        for (size_t i = 1; i < ips.size(); i++)
            std::cout << "             http://" << ips[i] << ":" << port << "/\n";
        std::cout << "\n";
    }

    // Download QR code image via API and open it
    std::string qrApiUrl = "https://api.2dcode.biz/v1/create-qr-code?data="
                           + urlEncode(url) + "&size=300x300";
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string qrPath = std::string(tempDir) + "stf_qrcode.png";

    std::cout << "  " << i18n::get("scan_qr") << "\n" << std::endl;

    HRESULT hr = URLDownloadToFileA(nullptr, qrApiUrl.c_str(), qrPath.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        ShellExecuteA(nullptr, "open", qrPath.c_str(), nullptr, nullptr, SW_SHOW);
        std::cout << "  " << i18n::get("qr_opened") << "\n\n";
    } else {
        std::cout << "  " << i18n::get("qr_failed") << "\n\n";
    }

    std::cout << "  " << i18n::get("press_ctrl_c") << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    while (true) {
        Sleep(1000);
    }

    WSACleanup();
    return 0;
}
