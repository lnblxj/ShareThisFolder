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
#include <sstream>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "urlmon.lib")

static HttpServer* g_server = nullptr;
static std::atomic<bool> g_quit{false};
static uint16_t g_upnpMappedPort = 0;

struct AddressEntry {
    std::string kind;
    std::string url;
};

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

static void clearConsole() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi = {};
    if (!GetConsoleScreenBufferInfo(out, &csbi)) return;

    DWORD cellCount = static_cast<DWORD>(csbi.dwSize.X) * csbi.dwSize.Y;
    DWORD written = 0;
    COORD home = {0, 0};

    FillConsoleOutputCharacterA(out, ' ', cellCount, home, &written);
    FillConsoleOutputAttribute(out, csbi.wAttributes, cellCount, home, &written);
    SetConsoleCursorPosition(out, home);
}

static void printAddressList(const std::vector<AddressEntry>& addresses) {
    for (size_t i = 0; i < addresses.size(); i++)
        std::cout << "  " << (i + 1) << ". " << addresses[i].kind << " "
                  << addresses[i].url << "\n";
}

static void renderMainMenu(const std::string& rootDir, const std::vector<AddressEntry>& addresses) {
    clearConsole();
    std::cout << "\n  " << i18n::get("title") << "\n"
              << "  ------------------------------------------------\n\n"
              << "  " << i18n::get("sharing_dir") << rootDir << "\n\n";

    printAddressList(addresses);

    std::cout << "\n  ------------------------------------------------\n"
              << "  1-" << addresses.size() << ". " << i18n::get("menu_hint") << "\n"
              << "  w. " << i18n::get("menu_enable_public") << "\n"
              << "  s. " << i18n::get("menu_show_qr") << "\n"
              << "  q. " << i18n::get("menu_quit") << "\n"
              << std::endl;
}

static void renderQrMenu(const std::vector<AddressEntry>& addresses, bool showInvalid) {
    clearConsole();
    std::cout << "\n  " << i18n::get("qr_menu_title") << "\n"
              << "  ------------------------------------------------\n\n";

    printAddressList(addresses);

    std::cout << "\n  ------------------------------------------------\n"
              << "  " << i18n::get("qr_prompt") << "\n";
    if (showInvalid)
        std::cout << "  " << i18n::get("qr_invalid") << "\n";
    std::cout << std::endl;
}

static int promptAddressIndexForQrCode(const std::vector<AddressEntry>& addresses) {
    if (addresses.empty()) return -1;

    bool showInvalid = false;
    while (!g_quit) {
        renderQrMenu(addresses, showInvalid);
        int ch = _getch();
        if (ch == 'q' || ch == 'Q') return -1;

        if (ch >= '1' && ch <= '9') {
            size_t idx = static_cast<size_t>(ch - '1');
            if (idx < addresses.size())
                return static_cast<int>(idx);
        }

        showInvalid = true;
    }

    return -1;
}

static bool isPrivateOrSpecialIPv4(const std::string& ip) {
    std::istringstream ss(ip);
    std::string part;
    int octets[4] = {};
    for (int i = 0; i < 4; i++) {
        if (!std::getline(ss, part, '.')) return true;
        try {
            octets[i] = std::stoi(part);
        } catch (...) {
            return true;
        }
        if (octets[i] < 0 || octets[i] > 255) return true;
    }
    if (std::getline(ss, part, '.')) return true;

    return octets[0] == 10 ||
           octets[0] == 127 ||
           octets[0] == 0 ||
           (octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127) ||
           (octets[0] == 169 && octets[1] == 254) ||
           (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
           (octets[0] == 192 && octets[1] == 168);
}

static std::string discoverPublicUrl(uint16_t port, const std::string& localIp) {
    std::string upnpExternalIp = upnpGetExternalIP();
    if (!upnpExternalIp.empty() && !isPrivateOrSpecialIPv4(upnpExternalIp)) {
        UpnpMapping mapping = upnpMapTcpPort(port, port, localIp, "ShareThisFolder");
        if (mapping.success && !isPrivateOrSpecialIPv4(mapping.externalIp)) {
            g_upnpMappedPort = mapping.externalPort;
            return "http://" + mapping.externalIp + ":" + std::to_string(mapping.externalPort) + "/";
        }
    }

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

    std::string publicUrl;

    std::vector<AddressEntry> addresses;
    for (auto& ip : ips)
        addresses.push_back({"LAN", "http://" + ip + ":" + std::to_string(port) + "/"});

    std::string rootDirUtf8 = wideToUtf8(wcwd);
    renderMainMenu(rootDirUtf8, addresses);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    while (!g_quit) {
        while (!_kbhit() && !g_quit) Sleep(100);
        if (g_quit) break;

        int ch = _getch();
        if (ch >= '1' && ch <= '9') {
            size_t idx = static_cast<size_t>(ch - '1');
            if (idx < addresses.size()) {
                copyToClipboard(addresses[idx].url);
                std::cout << "  " << i18n::get("copied") << "\n";
            }
        } else if (ch == 's' || ch == 'S') {
            int idx = promptAddressIndexForQrCode(addresses);
            if (idx >= 0) showQRCode(addresses[static_cast<size_t>(idx)].url);
            renderMainMenu(rootDirUtf8, addresses);
        } else if (ch == 'w' || ch == 'W') {
            if (!publicUrl.empty()) {
                std::cout << "  " << i18n::get("public_already_enabled") << " " << publicUrl << "\n";
                continue;
            }

            std::cout << "  " << i18n::get("public_starting") << "\n";
            publicUrl = discoverPublicUrl(port, localIp);
            if (!publicUrl.empty()) {
                addresses.push_back({"WAN", publicUrl});
                renderMainMenu(rootDirUtf8, addresses);
                std::cout << "  " << i18n::get("public_enabled") << " " << publicUrl << "\n";
            } else {
                renderMainMenu(rootDirUtf8, addresses);
                std::cout << "  " << i18n::get("public_failed") << "\n";
            }
        } else if (ch == 'q' || ch == 'Q') {
            g_quit = true;
            g_server->stop();
        }
    }

    tunnelStop();
    if (g_upnpMappedPort != 0) upnpRemovePortMapping(g_upnpMappedPort);
    WSACleanup();
    return 0;
}
