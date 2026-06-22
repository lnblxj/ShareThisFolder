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
#include <cwchar>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "urlmon.lib")

static HttpServer* g_server = nullptr;
static std::atomic<bool> g_quit{false};
static uint16_t g_upnpMappedPort = 0;
static const wchar_t* QUICK_LAUNCH_ARG = L"--stf-quick-launch-admin";
static const wchar_t* QUICK_LAUNCH_INSTALL = L"install";
static const wchar_t* QUICK_LAUNCH_REMOVE = L"remove";
static const wchar_t* QUICK_LAUNCH_DIR = L"C:\\Program Files\\ShareThisFolder";
static const wchar_t* QUICK_LAUNCH_EXE = L"C:\\Program Files\\ShareThisFolder\\stf.exe";

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

static void waitForAnyKey() {
    std::cout << "\n  " << i18n::get("press_any_key") << std::endl;
    _getch();
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
              << "  e. " << i18n::get("menu_settings") << "\n"
              << "  q. " << i18n::get("menu_quit") << "\n"
              << std::endl;
}

static void renderSettingsMenu(bool showInvalid) {
    clearConsole();
    std::cout << "\n  " << i18n::get("settings_title") << "\n"
              << "  ------------------------------------------------\n\n"
              << "  1. " << i18n::get("settings_install_quick_launch") << "\n"
              << "  2. " << i18n::get("settings_remove_quick_launch") << "\n"
              << "  q. " << i18n::get("settings_back") << "\n\n"
              << "  ------------------------------------------------\n"
              << "  " << i18n::get("settings_prompt") << "\n";
    if (showInvalid)
        std::cout << "  " << i18n::get("settings_invalid") << "\n";
    std::cout << std::endl;
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

static bool getCurrentExePath(wchar_t* path, DWORD size) {
    DWORD len = GetModuleFileNameW(nullptr, path, size);
    return len > 0 && len < size;
}

static bool readMachinePath(std::wstring& value) {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                            0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) return false;

    DWORD type = 0, bytes = 0;
    rc = RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_EXPAND_SZ && type != REG_SZ)) {
        RegCloseKey(key);
        return false;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    rc = RegQueryValueExW(key, L"Path", nullptr, &type,
                          reinterpret_cast<LPBYTE>(&buffer[0]), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;

    while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    value = buffer;
    return true;
}

static bool writeMachinePath(const std::wstring& value) {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                            0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS) return false;

    DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(key, L"Path", 0, REG_EXPAND_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 5000, nullptr);
    return true;
}

static std::wstring trimPathSegment(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'"'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'"' || s.back() == L'\\'))
        s.pop_back();
    return s;
}

static bool pathContainsDir(const std::wstring& pathValue, const std::wstring& dir) {
    std::wstring wanted = trimPathSegment(dir);
    size_t start = 0;
    while (start <= pathValue.size()) {
        size_t end = pathValue.find(L';', start);
        std::wstring part = pathValue.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (_wcsicmp(trimPathSegment(part).c_str(), wanted.c_str()) == 0)
            return true;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return false;
}

static std::wstring removeDirFromPath(const std::wstring& pathValue, const std::wstring& dir) {
    std::wstring wanted = trimPathSegment(dir);
    std::wstring result;
    size_t start = 0;
    while (start <= pathValue.size()) {
        size_t end = pathValue.find(L';', start);
        std::wstring part = pathValue.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!part.empty() && _wcsicmp(trimPathSegment(part).c_str(), wanted.c_str()) != 0) {
            if (!result.empty()) result += L";";
            result += part;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return result;
}

static bool installQuickLaunchElevated(std::string& message) {
    wchar_t currentExe[MAX_PATH] = {};
    if (!getCurrentExePath(currentExe, MAX_PATH)) {
        message = i18n::get("quick_launch_error_current_exe");
        return false;
    }

    if (!CreateDirectoryW(QUICK_LAUNCH_DIR, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        message = i18n::get("quick_launch_error_create_dir");
        return false;
    }

    if (!CopyFileW(currentExe, QUICK_LAUNCH_EXE, FALSE)) {
        message = i18n::get("quick_launch_error_copy");
        return false;
    }

    std::wstring pathValue;
    if (!readMachinePath(pathValue)) {
        message = i18n::get("quick_launch_error_path_read");
        return false;
    }

    if (!pathContainsDir(pathValue, QUICK_LAUNCH_DIR)) {
        if (!pathValue.empty() && pathValue.back() != L';') pathValue += L";";
        pathValue += QUICK_LAUNCH_DIR;
        if (!writeMachinePath(pathValue)) {
            message = i18n::get("quick_launch_error_path_write");
            return false;
        }
    }

    message = std::string(i18n::get("quick_launch_installed")) + " stf";
    return true;
}

static bool removeQuickLaunchElevated(std::string& message) {
    std::wstring pathValue;
    if (!readMachinePath(pathValue)) {
        message = i18n::get("quick_launch_error_path_read");
        return false;
    }

    std::wstring newPath = removeDirFromPath(pathValue, QUICK_LAUNCH_DIR);
    if (newPath != pathValue && !writeMachinePath(newPath)) {
        message = i18n::get("quick_launch_error_path_write");
        return false;
    }

    DWORD attrs = GetFileAttributesW(QUICK_LAUNCH_EXE);
    if (attrs != INVALID_FILE_ATTRIBUTES && !DeleteFileW(QUICK_LAUNCH_EXE)) {
        message = i18n::get("quick_launch_error_delete");
        return false;
    }

    RemoveDirectoryW(QUICK_LAUNCH_DIR);
    message = i18n::get("quick_launch_removed");
    return true;
}

static int runQuickLaunchAdminCommand(const wchar_t* action) {
    SetConsoleOutputCP(65001);
    i18n::init();
    std::string message;
    bool ok = false;
    if (wcscmp(action, QUICK_LAUNCH_INSTALL) == 0)
        ok = installQuickLaunchElevated(message);
    else if (wcscmp(action, QUICK_LAUNCH_REMOVE) == 0)
        ok = removeQuickLaunchElevated(message);
    else
        message = "Invalid quick launch action";

    std::cout << "\n  " << message << "\n";
    return ok ? 0 : 1;
}

static bool runElevatedQuickLaunchAction(const wchar_t* action) {
    wchar_t exePath[MAX_PATH] = {};
    if (!getCurrentExePath(exePath, MAX_PATH)) return false;

    std::wstring params = L"\"";
    params += QUICK_LAUNCH_ARG;
    params += L"\" \"";
    params += action;
    params += L"\"";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) return false;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return exitCode == 0;
}

static void showQuickLaunchResult(bool install, bool ok) {
    clearConsole();
    std::cout << "\n  " << i18n::get("settings_title") << "\n"
              << "  ------------------------------------------------\n\n";

    if (ok) {
        if (install) {
            std::cout << "  " << i18n::get("quick_launch_installed") << "\n\n"
                      << "  " << i18n::get("quick_launch_usage") << "\n"
                      << "  stf\n";
        } else {
            std::cout << "  " << i18n::get("quick_launch_removed") << "\n";
        }
    } else {
        std::cout << "  " << i18n::get("quick_launch_failed") << "\n";
    }

    waitForAnyKey();
}

static void promptSettingsMenu() {
    bool showInvalid = false;
    while (!g_quit) {
        renderSettingsMenu(showInvalid);
        int ch = _getch();
        if (ch == 'q' || ch == 'Q') return;

        if (ch == '1') {
            bool ok = runElevatedQuickLaunchAction(QUICK_LAUNCH_INSTALL);
            showQuickLaunchResult(true, ok);
            showInvalid = false;
        } else if (ch == '2') {
            bool ok = runElevatedQuickLaunchAction(QUICK_LAUNCH_REMOVE);
            showQuickLaunchResult(false, ok);
            showInvalid = false;
        } else {
            showInvalid = true;
        }
    }
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

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 3 && wcscmp(argv[1], QUICK_LAUNCH_ARG) == 0)
        return runQuickLaunchAdminCommand(argv[2]);

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
        } else if (ch == 'e' || ch == 'E') {
            promptSettingsMenu();
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
