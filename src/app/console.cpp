#include "share_this_folder/app/console.h"
#include "share_this_folder/util/i18n.h"
#include <windows.h>
#include <conio.h>
#include <iostream>

namespace console_ui {

void clear() {
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

void waitForAnyKey() {
    std::cout << "\n  " << i18n::get("press_any_key") << std::endl;
    _getch();
}

} // namespace console_ui
