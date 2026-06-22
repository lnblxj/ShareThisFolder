#pragma once
#include <cwchar>

namespace quick_launch {

bool isAdminCommand(int argc, wchar_t* argv[]);
int runAdminCommand(int argc, wchar_t* argv[]);
bool installElevated();
bool removeElevated();

} // namespace quick_launch
