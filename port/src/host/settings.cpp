#include "host/settings.h"

#include <fstream>
#include <map>

#include "host/host.h"
#include "psx/log.h"

namespace settings {
namespace {

std::map<std::string, std::string> g_values;

std::string path() {
    return host::user_dir() + "settings.ini";
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

void load() {
    g_values.clear();
    std::ifstream f(path());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) g_values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

std::string get(const std::string &key, const std::string &fallback) {
    auto it = g_values.find(key);
    return it == g_values.end() ? fallback : it->second;
}

void set(const std::string &key, const std::string &value) {
    g_values[key] = value;
    std::ofstream f(path());
    if (!f) {
        LOGW("Could not save %s", path().c_str());
        return;
    }
    f << "# PaRappa the Rapper port settings\n";
    for (const auto &kv : g_values) f << kv.first << " = " << kv.second << "\n";
}

}  // namespace settings
