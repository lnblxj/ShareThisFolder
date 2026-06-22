#include "share_this_folder/app/settings_menu.h"
#include "share_this_folder/app/console.h"
#include "share_this_folder/app/quick_launch.h"
#include "share_this_folder/util/i18n.h"
#include <conio.h>
#include <iostream>

static void renderSettingsMenu(bool showInvalid) {
    console_ui::clear();
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

static void showQuickLaunchResult(bool install, bool ok) {
    console_ui::clear();
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

    console_ui::waitForAnyKey();
}

void promptSettingsMenu(std::atomic<bool>& quit) {
    bool showInvalid = false;
    while (!quit) {
        renderSettingsMenu(showInvalid);
        int ch = _getch();
        if (ch == 'q' || ch == 'Q') return;

        if (ch == '1') {
            bool ok = quick_launch::installElevated();
            showQuickLaunchResult(true, ok);
            showInvalid = false;
        } else if (ch == '2') {
            bool ok = quick_launch::removeElevated();
            showQuickLaunchResult(false, ok);
            showInvalid = false;
        } else {
            showInvalid = true;
        }
    }
}
