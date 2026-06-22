#include "share_this_folder/app/quick_launch.h"
#include "share_this_folder/util/i18n.h"
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <iostream>
#include <string>

namespace quick_launch {
namespace {

const wchar_t* ADMIN_ARG = L"--stf-quick-launch-admin";
const wchar_t* INSTALL = L"install";
const wchar_t* REMOVE = L"remove";
const wchar_t* INSTALL_DIR = L"C:\\Program Files\\ShareThisFolder";
const wchar_t* INSTALL_EXE = L"C:\\Program Files\\ShareThisFolder\\stf.exe";

bool getCurrentExePath(wchar_t* path, DWORD size) {
    DWORD len = GetModuleFileNameW(nullptr, path, size);
    return len > 0 && len < size;
}

bool readMachinePath(std::wstring& value) {
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

bool writeMachinePath(const std::wstring& value) {
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

std::wstring trimPathSegment(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'"'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'"' || s.back() == L'\\'))
        s.pop_back();
    return s;
}

bool pathContainsDir(const std::wstring& pathValue, const std::wstring& dir) {
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

std::wstring removeDirFromPath(const std::wstring& pathValue, const std::wstring& dir) {
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

bool install(std::string& message) {
    wchar_t currentExe[MAX_PATH] = {};
    if (!getCurrentExePath(currentExe, MAX_PATH)) {
        message = i18n::get("quick_launch_error_current_exe");
        return false;
    }

    if (!CreateDirectoryW(INSTALL_DIR, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        message = i18n::get("quick_launch_error_create_dir");
        return false;
    }

    if (_wcsicmp(currentExe, INSTALL_EXE) != 0 && !CopyFileW(currentExe, INSTALL_EXE, FALSE)) {
        message = i18n::get("quick_launch_error_copy");
        return false;
    }

    std::wstring pathValue;
    if (!readMachinePath(pathValue)) {
        message = i18n::get("quick_launch_error_path_read");
        return false;
    }

    if (!pathContainsDir(pathValue, INSTALL_DIR)) {
        if (!pathValue.empty() && pathValue.back() != L';') pathValue += L";";
        pathValue += INSTALL_DIR;
        if (!writeMachinePath(pathValue)) {
            message = i18n::get("quick_launch_error_path_write");
            return false;
        }
    }

    message = std::string(i18n::get("quick_launch_installed")) + " stf";
    return true;
}

bool remove(std::string& message) {
    std::wstring pathValue;
    if (!readMachinePath(pathValue)) {
        message = i18n::get("quick_launch_error_path_read");
        return false;
    }

    std::wstring newPath = removeDirFromPath(pathValue, INSTALL_DIR);
    if (newPath != pathValue && !writeMachinePath(newPath)) {
        message = i18n::get("quick_launch_error_path_write");
        return false;
    }

    DWORD attrs = GetFileAttributesW(INSTALL_EXE);
    if (attrs != INVALID_FILE_ATTRIBUTES && !DeleteFileW(INSTALL_EXE)) {
        message = i18n::get("quick_launch_error_delete");
        return false;
    }

    RemoveDirectoryW(INSTALL_DIR);
    message = i18n::get("quick_launch_removed");
    return true;
}

bool runElevatedAction(const wchar_t* action) {
    wchar_t exePath[MAX_PATH] = {};
    if (!getCurrentExePath(exePath, MAX_PATH)) return false;

    std::wstring params = L"\"";
    params += ADMIN_ARG;
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

} // namespace

bool isAdminCommand(int argc, wchar_t* argv[]) {
    return argc >= 3 && wcscmp(argv[1], ADMIN_ARG) == 0;
}

int runAdminCommand(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(65001);
    i18n::init();

    if (!isAdminCommand(argc, argv)) return 1;

    std::string message;
    bool ok = false;
    if (wcscmp(argv[2], INSTALL) == 0)
        ok = install(message);
    else if (wcscmp(argv[2], REMOVE) == 0)
        ok = remove(message);
    else
        message = "Invalid quick launch action";

    std::cout << "\n  " << message << "\n";
    return ok ? 0 : 1;
}

bool installElevated() {
    return runElevatedAction(INSTALL);
}

bool removeElevated() {
    return runElevatedAction(REMOVE);
}

} // namespace quick_launch
