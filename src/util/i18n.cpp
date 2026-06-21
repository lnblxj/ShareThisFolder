#include "share_this_folder/util/i18n.h"
#include <windows.h>
#include <map>
#include <string>

namespace i18n {

static Lang g_lang = Lang::EN;

static const std::map<std::string, const char*> zh = {
    {"title",           "ShareThisFolder - 文件共享"},
    {"sharing_dir",     "共享目录: "},
    {"menu_hint",       "按下对应数字复制到剪贴板:"},
    {"menu_show_qr",    "展示二维码"},
    {"menu_quit",       "退出"},
    {"copied",          "已复制"},
    {"error_listen",    "错误: 监听失败"},
    {"error_ip",        "错误: 无法获取本机 IP 地址"},
    {"file_not_found",  "404 文件未找到"},
    {"dir_index",       "目录列表"},
    {"parent_dir",      "上级目录"},
};

static const std::map<std::string, const char*> en = {
    {"title",           "ShareThisFolder - File Sharing"},
    {"sharing_dir",     "Sharing: "},
    {"menu_hint",       "Copy to clipboard:"},
    {"menu_show_qr",    "Show QR code"},
    {"menu_quit",       "Quit"},
    {"copied",          "Copied"},
    {"error_listen",    "Error: Listen failed"},
    {"error_ip",        "Error: Cannot get local IP address"},
    {"file_not_found",  "404 Not Found"},
    {"dir_index",       "Index of"},
    {"parent_dir",      "Parent Directory"},
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
