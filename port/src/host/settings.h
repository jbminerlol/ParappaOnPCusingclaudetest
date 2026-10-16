// Persistent settings (settings.ini in the user folder): simple key = value lines.
#pragma once
#include <string>

namespace settings {

void load();
std::string get(const std::string &key, const std::string &fallback = "");
void set(const std::string &key, const std::string &value);  // saves immediately

}  // namespace settings
