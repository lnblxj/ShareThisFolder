#include "share_this_folder/http/server.h"
#include "share_this_folder/net/network.h"
#include "share_this_folder/net/stun_client.h"
#include "share_this_folder/net/upnp_client.h"
#include "share_this_folder/util/i18n.h"
#include <winsock2.h>
#include <windows.h>
#include <urlmon.h>
#include <shellapi.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <csignal>
#include <conio.h>
#include <atomic>
#include <thread>
#include <vector>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "urlmon.lib")

static HttpServer* g_server = nullptr;
static std::atomic<bool> g_quit{false};

static void signalHandler(int) {
    g_quit = true;
    if (g_server) g_server->stop();
}

static std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &result[0], len, nullptr, nullptr);
    return result;
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

static void copyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hMem) { CloseClipboard(); return; }
    char* p = static_cast<char*>(GlobalLock(hMem));
    memcpy(p, text.c_str(), text.size() + 1);
    GlobalUnlock(hMem);
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
}

static void showQRCode(const std::string& url) {
    std::string qrUrl = "https://api.2dcode.biz/v1/create-qr-code?data=" + urlEncode(url) + "&size=300x300";
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string qrPath = std::string(tempDir) + "stf_qrcode.png";
    if (SUCCEEDED(URLDownloadToFileA(nullptr, qrUrl.c_str(), qrPath.c_str(), 0, nullptr)))
        ShellExecuteA(nullptr, "open", qrPath.c_str(), nullptr, nullptr, SW_SHOW);
}

static std::string discoverPublicUrl(uint16_t port, const std::string& localIp) {
    std::string externalIp = upnpGetExternalIP();
    if (!externalIp.empty() && upnpAddPortMapping(port, port, localIp, "ShareThisFolder"))
        return "http://" + externalIp + ":" + std::to_string(port) + "/";

    StunResult r;
    if (tunnelStart("www.baidu.com", 80, "stun.nextcloud.com", 3478, port, "0.0.0.0", r))
        return "http://" + r.ip + ":" + std::to_string(r.port) + "/";

    return "";
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    SetConsoleOutputCP(65001);
    i18n::init();

    wchar_t wcwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, wcwd);
    std::filesystem::path rootDir(wcwd);

    auto ips = getLocalIPs();
    if (ips.empty()) {
        std::cerr << i18n::get("error_ip") << "\n";
        WSACleanup();
        return 1;
    }

    std::string localIp = ips[0];
    int port = findAvailablePort(8888);

    HttpServer server(rootDir, port);
    g_server = &server;
    if (!server.start()) {
        std::cerr << i18n::get("error_listen") << "\n";
        WSACleanup();
        return 1;
    }

    std::string publicUrl = discoverPublicUrl(port, localIp);

    std::vector<std::string> urls;
    for (auto& ip : ips)
        urls.push_back("http://" + ip + ":" + std::to_string(port) + "/");

    std::cout << "\n  " << i18n::get("title") << "\n"
              << "  ------------------------------------------------\n\n"
              << "  " << i18n::get("sharing_dir") << wideToUtf8(wcwd) << "\n\n";

    for (size_t i = 0; i < urls.size(); i++)
        std::cout << "  LAN " << (i + 1) << ". " << urls[i] << "\n";

    if (!publicUrl.empty())
        std::cout << "  WAN    " << publicUrl << "\n";

    std::cout << "\n  ------------------------------------------------\n"
              << "  1-" << urls.size() << ". " << i18n::get("menu_hint") << "\n"
              << "  s. " << i18n::get("menu_show_qr") << "\n"
              << "  q. " << i18n::get("menu_quit") << "\n"
              << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    while (!g_quit) {
        while (!_kbhit() && !g_quit) Sleep(100);
        if (g_quit) break;

        int ch = _getch();
        if (ch >= '1' && ch <= '9') {
            size_t idx = static_cast<size_t>(ch - '1');
            if (idx < urls.size()) {
                copyToClipboard(urls[idx]);
                std::cout << "  " << i18n::get("copied") << "\n";
            }
        } else if (ch == 's' || ch == 'S') {
            showQRCode(publicUrl.empty() ? urls[0] : publicUrl);
        } else if (ch == 'q' || ch == 'Q') {
            g_quit = true;
            g_server->stop();
        }
    }

    tunnelStop();
    WSACleanup();
    return 0;
}
