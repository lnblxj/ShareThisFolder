#include "share_this_folder/util/i18n.h"
#include <windows.h>
#include <map>
#include <string>

namespace i18n {

static Lang g_lang = Lang::EN;

static const std::map<std::string, const char*> zh = {
    {"title",            "ShareThisFolder"},
    {"sharing_dir",      "共享目录: "},
    {"menu_hint",        "按下对应序号复制到剪贴板"},
    {"menu_enable_public", "开启公网访问"},
    {"menu_show_qr",     "显示二维码"},
    {"menu_settings",    "设置"},
    {"menu_quit",        "退出"},
    {"copied",           "已复制"},
    {"public_starting",  "正在开启公网访问..."},
    {"public_enabled",   "公网访问已开启:"},
    {"public_failed",    "公网访问开启失败"},
    {"public_already_enabled", "公网访问已开启:"},
    {"qr_menu_title",    "显示二维码"},
    {"qr_prompt",        "按下对应数字显示二维码，按 q 取消"},
    {"qr_invalid",       "无效的地址序号"},
    {"settings_title",   "设置"},
    {"settings_install_quick_launch", "设置快捷唤起"},
    {"settings_remove_quick_launch", "移除快捷唤起"},
    {"settings_back",    "返回"},
    {"settings_prompt",  "选择设置项，或按 q 返回"},
    {"settings_invalid", "无效的设置项"},
    {"quick_launch_installed", "快捷唤起已设置。以后可在 Windows 资源管理器地址栏输入以下命令并回车:"},
    {"quick_launch_removed", "快捷唤起已移除"},
    {"quick_launch_usage", "快速唤起方式:"},
    {"quick_launch_failed", "操作未完成，请确认已允许管理员权限"},
    {"quick_launch_error_current_exe", "错误: 无法获取当前程序路径"},
    {"quick_launch_error_create_dir", "错误: 无法创建安装目录"},
    {"quick_launch_error_copy", "错误: 无法复制 stf.exe"},
    {"quick_launch_error_path_read", "错误: 无法读取系统环境变量 Path"},
    {"quick_launch_error_path_write", "错误: 无法写入系统环境变量 Path"},
    {"quick_launch_error_delete", "错误: 无法删除 stf.exe"},
    {"press_any_key",    "按任意键返回主菜单"},
    {"error_listen",     "错误: 监听失败"},
    {"error_ip",         "错误: 无法获取本机 IP 地址"},
    {"file_not_found",   "404 文件未找到"},
    {"dir_index",        "目录列表"},
    {"parent_dir",       "上级目录"},
};

static const std::map<std::string, const char*> en = {
    {"title",            "ShareThisFolder"},
    {"sharing_dir",      "Sharing: "},
    {"menu_hint",        "Copy to clipboard"},
    {"menu_enable_public", "Enable public access"},
    {"menu_show_qr",     "Show QR code"},
    {"menu_settings",    "Settings"},
    {"menu_quit",        "Quit"},
    {"copied",           "Copied"},
    {"public_starting",  "Enabling public access..."},
    {"public_enabled",   "Public access enabled:"},
    {"public_failed",    "Failed to enable public access"},
    {"public_already_enabled", "Public access already enabled:"},
    {"qr_menu_title",    "Show QR code"},
    {"qr_prompt",        "Press an address number to show QR code, or q to cancel"},
    {"qr_invalid",       "Invalid address number"},
    {"settings_title",   "Settings"},
    {"settings_install_quick_launch", "Set quick launch"},
    {"settings_remove_quick_launch", "Remove quick launch"},
    {"settings_back",    "Back"},
    {"settings_prompt",  "Choose a setting, or press q to go back"},
    {"settings_invalid", "Invalid setting"},
    {"quick_launch_installed", "Quick launch is set. In Windows File Explorer's address bar, type this command and press Enter:"},
    {"quick_launch_removed", "Quick launch has been removed"},
    {"quick_launch_usage", "Quick launch command:"},
    {"quick_launch_failed", "Operation did not finish. Confirm that administrator permission was allowed"},
    {"quick_launch_error_current_exe", "Error: Cannot get current executable path"},
    {"quick_launch_error_create_dir", "Error: Cannot create install directory"},
    {"quick_launch_error_copy", "Error: Cannot copy stf.exe"},
    {"quick_launch_error_path_read", "Error: Cannot read system Path environment variable"},
    {"quick_launch_error_path_write", "Error: Cannot write system Path environment variable"},
    {"quick_launch_error_delete", "Error: Cannot delete stf.exe"},
    {"press_any_key",    "Press any key to return to the main menu"},
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
