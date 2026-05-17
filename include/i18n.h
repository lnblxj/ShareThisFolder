#pragma once
#include <string>

namespace i18n {

enum class Lang { EN, ZH };

void init();
Lang getLang();
const char* get(const char* key);

} // namespace i18n
