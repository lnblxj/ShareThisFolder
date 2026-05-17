#include "i18n.h"
#include <windows.h>
#include <map>
#include <string>

namespace i18n {

static Lang g_lang = Lang::EN;

static const std::map<std::string, const char*> zh = {
    {"title",           "ShareThisFolder - 局域网文件共享"},
    {"server_starting", "正在启动服务器..."},
    {"server_ready",    "服务器已启动!"},
    {"scan_qr",         "手机扫描二维码访问:"},
    {"open_url",        "或在浏览器打开: "},
    {"sharing_dir",     "共享目录: "},
    {"press_ctrl_c",    "按 Ctrl+C 停止服务器"},
    {"error_wsa",       "错误: Winsock 初始化失败"},
    {"error_bind",      "错误: 无法绑定端口 "},
    {"error_listen",    "错误: 监听失败"},
    {"error_ip",        "错误: 无法获取本机 IP 地址"},
    {"file_not_found",  "404 文件未找到"},
    {"dir_index",       "目录列表"},
    {"parent_dir",      "上级目录"},
    {"col_name",        "名称"},
    {"col_size",        "大小"},
    {"col_time",        "修改时间"},
    {"dir_label",       "[目录]"},
    {"qr_opened",       "二维码图片已在图片查看器中打开，请用手机扫描"},
    {"qr_failed",       "网络不可用，无法生成二维码图片，请手动在浏览器中打开上方链接"},
};

static const std::map<std::string, const char*> en = {
    {"title",           "ShareThisFolder - LAN File Sharing"},
    {"server_starting", "Starting server..."},
    {"server_ready",    "Server is ready!"},
    {"scan_qr",         "Scan QR code with your phone:"},
    {"open_url",        "Or open in browser: "},
    {"sharing_dir",     "Sharing: "},
    {"press_ctrl_c",    "Press Ctrl+C to stop"},
    {"error_wsa",       "Error: Failed to initialize Winsock"},
    {"error_bind",      "Error: Cannot bind port "},
    {"error_listen",    "Error: Listen failed"},
    {"error_ip",        "Error: Cannot get local IP address"},
    {"file_not_found",  "404 File Not Found"},
    {"dir_index",       "Index of"},
    {"parent_dir",      "Parent Directory"},
    {"col_name",        "Name"},
    {"col_size",        "Size"},
    {"col_time",        "Modified"},
    {"dir_label",       "[DIR]"},
    {"qr_opened",       "QR code image opened - scan with your phone"},
    {"qr_failed",       "Network unavailable - open the URL above in your phone browser"},
};

void init() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD primary = langId & 0x3FF;
    g_lang = (primary == 0x04) ? Lang::ZH : Lang::EN;
}

Lang getLang() {
    return g_lang;
}

const char* get(const char* key) {
    auto& table = (g_lang == Lang::ZH) ? zh : en;
    auto it = table.find(key);
    if (it != table.end()) return it->second;
    return key;
}

} // namespace i18n
