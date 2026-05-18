#include "server.h"
#include "i18n.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

static std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &result[0], len, nullptr, nullptr);
    return result;
}

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], len);
    return result;
}

static std::string pathToUtf8(const fs::path& p) {
    return wideToUtf8(p.wstring());
}

static std::string urlEncodeUtf8(const std::string& utf8) {
    std::string result;
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
            result += static_cast<char>(c);
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

HttpServer::HttpServer(const fs::path& rootDir, int port)
    : rootDir_(rootDir), port_(port), listenSock_(INVALID_SOCKET), running_(false) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
    listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock_ == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(static_cast<SOCKET>(listenSock_), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(static_cast<SOCKET>(listenSock_),
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(static_cast<SOCKET>(listenSock_));
        return false;
    }

    if (port_ == 0) {
        int len = sizeof(addr);
        getsockname(static_cast<SOCKET>(listenSock_),
                    reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }

    if (listen(static_cast<SOCKET>(listenSock_), SOMAXCONN) != 0) {
        closesocket(static_cast<SOCKET>(listenSock_));
        return false;
    }

    running_ = true;
    thread_ = std::thread(&HttpServer::run, this);
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listenSock_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listenSock_));
        listenSock_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
}

int HttpServer::getPort() const { return port_; }

void HttpServer::run() {
    while (running_) {
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET client = accept(static_cast<SOCKET>(listenSock_),
                               reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (client == INVALID_SOCKET) continue;
        std::thread([this, client]() {
            handleClient(static_cast<intptr_t>(client));
        }).detach();
    }
}

void HttpServer::handleClient(intptr_t clientSock) {
    SOCKET sock = static_cast<SOCKET>(clientSock);
    char buf[4096] = {};
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { closesocket(sock); return; }
    buf[n] = '\0';

    std::string request(buf, n);
    std::string method, path;
    {
        std::istringstream iss(request);
        iss >> method >> path;
    }

    if (method != "GET") {
        std::string resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
        closesocket(sock);
        return;
    }

    // urlDecode gives us UTF-8 bytes (browser sends percent-encoded UTF-8)
    path = urlDecode(path);
    if (path.empty() || path[0] != '/') path = "/" + path;

    // Build disk path: rootDir_ is already fs::path, append UTF-8 relative part via wide string
    fs::path diskPath = rootDir_;
    if (path.size() > 1) {
        diskPath /= utf8ToWide(path.substr(1));
    }
    diskPath = diskPath.lexically_normal();

    // Security: ensure path is under rootDir_
    {
        fs::path rootNorm = rootDir_.lexically_normal();
        auto diskW = diskPath.wstring();
        auto rootW = rootNorm.wstring();
        bool ok = (diskW.size() >= rootW.size() &&
                   diskW.compare(0, rootW.size(), rootW) == 0 &&
                   (diskW.size() == rootW.size() || rootW.back() == L'\\' || diskW[rootW.size()] == L'\\'));
        if (!ok) {
            std::string body = i18n::get("file_not_found");
            std::ostringstream oss;
            oss << "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain; charset=utf-8\r\n"
                << "Content-Length: " << body.size() << "\r\n\r\n" << body;
            std::string resp = oss.str();
            send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
            closesocket(sock);
            return;
        }
    }

    if (fs::is_directory(diskPath)) {
        if (path.back() != '/') {
            std::ostringstream oss;
            oss << "HTTP/1.1 301 Moved Permanently\r\nLocation: " << path << "/\r\n\r\n";
            std::string resp = oss.str();
            send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
            closesocket(sock);
            return;
        }
        std::string body = buildDirListing(diskPath, path);
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        std::string resp = oss.str();
        send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
    } else if (fs::exists(diskPath)) {
        std::ifstream file(diskPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::string body = i18n::get("file_not_found");
            std::ostringstream oss;
            oss << "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\n"
                << "Content-Length: " << body.size() << "\r\n\r\n" << body;
            std::string resp = oss.str();
            send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
            closesocket(sock);
            return;
        }

        auto fileSize = file.tellg();
        file.seekg(0);

        std::string ext = pathToUtf8(diskPath.extension());
        std::string mime = getMimeType(ext);
        std::string fileNameUtf8 = pathToUtf8(diskPath.filename());

        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << mime << "\r\n"
            << "Content-Length: " << fileSize << "\r\n"
            << "Content-Disposition: inline; filename*=UTF-8''" << urlEncodeUtf8(fileNameUtf8) << "\r\n"
            << "Connection: close\r\n\r\n";
        std::string header = oss.str();
        send(sock, header.c_str(), static_cast<int>(header.size()), 0);

        char fbuf[65536];
        while (file.read(fbuf, sizeof(fbuf)) || file.gcount() > 0) {
            int toSend = static_cast<int>(file.gcount());
            int sent = 0;
            while (sent < toSend) {
                int s = send(sock, fbuf + sent, toSend - sent, 0);
                if (s <= 0) break;
                sent += s;
            }
            if (!file && file.gcount() == 0) break;
        }
    } else {
        std::string body = i18n::get("file_not_found");
        std::ostringstream oss;
        oss << "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\n"
            << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        std::string resp = oss.str();
        send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
    }

    closesocket(sock);
}

std::string HttpServer::buildDirListing(const fs::path& diskPath, const std::string& urlPath) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         << "<title>" << i18n::get("dir_index") << " " << urlPath << "</title>"
         << "<style>"
         << "*{box-sizing:border-box;margin:0;padding:0}"
         << "body{font-family:-apple-system,Segoe UI,sans-serif;padding:16px;background:#f5f5f5;color:#333}"
         << "h1{font-size:18px;margin-bottom:12px;word-break:break-all}"
         << ".list{background:#fff;border-radius:8px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
         << ".row{display:flex;align-items:center;padding:10px 16px;border-bottom:1px solid #eee;text-decoration:none;color:#333}"
         << ".row:last-child{border-bottom:none}"
         << ".row:hover{background:#f0f7ff}"
         << ".icon{width:28px;flex-shrink:0;text-align:center}"
         << ".name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
         << ".size{color:#999;font-size:13px;margin-left:12px;flex-shrink:0}"
         << "a.row{color:#0066cc}"
         << "</style></head><body>"
         << "<h1>" << i18n::get("dir_index") << " " << urlPath << "</h1><div class=\"list\">";

    if (urlPath != "/") {
        std::string parent = urlPath.substr(0, urlPath.find_last_of('/', urlPath.size() - 2) + 1);
        html << "<a class=\"row\" href=\"" << parent << "\">"
             << "<span class=\"icon\">\xf0\x9f\x93\x82</span>"
             << "<span class=\"name\">" << i18n::get("parent_dir") << "</span></a>";
    }

    std::vector<fs::directory_entry> dirs, files;
    for (auto& entry : fs::directory_iterator(diskPath)) {
        if (entry.is_directory()) dirs.push_back(entry);
        else files.push_back(entry);
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    for (auto& d : dirs) {
        std::string nameUtf8 = pathToUtf8(d.path().filename());
        std::string encodedName = urlEncodeUtf8(nameUtf8);
        html << "<a class=\"row\" href=\"" << urlPath << encodedName << "/\">"
             << "<span class=\"icon\">\xf0\x9f\x93\x81</span>"
             << "<span class=\"name\">" << nameUtf8 << "/</span>"
             << "<span class=\"size\">-</span></a>";
    }

    for (auto& f : files) {
        std::string nameUtf8 = pathToUtf8(f.path().filename());
        std::string encodedName = urlEncodeUtf8(nameUtf8);
        uint64_t sz = f.file_size();
        html << "<a class=\"row\" href=\"" << urlPath << encodedName << "\">"
             << "<span class=\"icon\">\xf0\x9f\x93\x84</span>"
             << "<span class=\"name\">" << nameUtf8 << "</span>"
             << "<span class=\"size\">" << formatSize(sz) << "</span></a>";
    }

    html << "</div></body></html>";
    return html.str();
}

std::string HttpServer::getMimeType(const std::string& ext) {
    static const std::pair<const char*, const char*> types[] = {
        {".html","text/html"},{".htm","text/html"},{".css","text/css"},
        {".js","application/javascript"},{".json","application/json"},
        {".xml","application/xml"},{".txt","text/plain"},{".csv","text/csv"},
        {".png","image/png"},{".jpg","image/jpeg"},{".jpeg","image/jpeg"},
        {".gif","image/gif"},{".svg","image/svg+xml"},{".ico","image/x-icon"},
        {".webp","image/webp"},{".bmp","image/bmp"},
        {".mp3","audio/mpeg"},{".wav","audio/wav"},{".ogg","audio/ogg"},
        {".mp4","video/mp4"},{".avi","video/x-msvideo"},{".mkv","video/x-matroska"},
        {".webm","video/webm"},{".mov","video/quicktime"},
        {".pdf","application/pdf"},{".zip","application/zip"},
        {".gz","application/gzip"},{".tar","application/x-tar"},
        {".7z","application/x-7z-compressed"},{".rar","application/vnd.rar"},
        {".doc","application/msword"},
        {".docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls","application/vnd.ms-excel"},
        {".xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt","application/vnd.ms-powerpoint"},
        {".pptx","application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {".wasm","application/wasm"},{".apk","application/vnd.android.package-archive"},
    };

    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (auto& [e, mime] : types)
        if (lower == e) return mime;
    return "application/octet-stream";
}

std::string HttpServer::urlDecode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int val = 0;
            for (int j = 1; j <= 2; j++) {
                char c = s[i + j];
                val <<= 4;
                if (c >= '0' && c <= '9') val |= c - '0';
                else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
                else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
            }
            result += static_cast<char>(val);
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string HttpServer::formatSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double sz = static_cast<double>(bytes);
    int i = 0;
    while (sz >= 1024.0 && i < 4) { sz /= 1024.0; i++; }
    std::ostringstream oss;
    if (i == 0) oss << bytes << " B";
    else oss << std::fixed << std::setprecision(1) << sz << " " << units[i];
    return oss.str();
}
