#include "share_this_folder/util/i18n.h"
#include <windows.h>
#include <map>
#include <string>

namespace i18n {

static Lang g_lang = Lang::EN;

static const std::map<std::string, const char*> zh = {
    {"title",            "ShareThisFolder"},
    {"sharing_dir",      "共享目录: "},
    {"menu_hint",        "按下对应数字复制到剪贴板:"},
    {"menu_show_qr",     "显示二维码"},
    {"menu_quit",        "退出"},
    {"copied",           "已复制"},
    {"qr_menu_title",    "显示二维码"},
    {"qr_prompt",        "按下对应数字显示二维码，按 q 取消"},
    {"qr_invalid",       "无效的地址序号"},
    {"error_listen",     "错误: 监听失败"},
    {"error_ip",         "错误: 无法获取本机 IP 地址"},
    {"file_not_found",   "404 文件未找到"},
    {"dir_index",        "目录列表"},
    {"parent_dir",       "上级目录"},
};

static const std::map<std::string, const char*> en = {
    {"title",            "ShareThisFolder"},
    {"sharing_dir",      "Sharing: "},
    {"menu_hint",        "Copy to clipboard:"},
    {"menu_show_qr",     "Show QR code"},
    {"menu_quit",        "Quit"},
    {"copied",           "Copied"},
    {"qr_menu_title",    "Show QR code"},
    {"qr_prompt",        "Press an address number to show QR code, or q to cancel"},
    {"qr_invalid",       "Invalid address number"},
    {"error_listen",     "Error: Listen failed"},
    {"error_ip",         "Error: Cannot get local IP address"},
    {"file_not_found",   "404 Not Found"},
    {"dir_index",        "Index of"},
    {"parent_dir",       "Parent Directory"},
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
