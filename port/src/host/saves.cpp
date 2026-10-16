// Where the memory card is kept: memcard\bu00 next to the executable, so the game folder carries its
// saves. Security software (such as Trend Micro Folder Shield) can kill a program that creates files
// in a protected folder, so a child copy of the executable tries it first; if that copy does not come
// back, the saves go to the user folder instead. The answer is kept in settings.ini per game folder.
#include <windows.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "host/host.h"
#include "host/settings.h"
#include "psx/log.h"

namespace fs = std::filesystem;

namespace host {
namespace {

constexpr int kWriteTestOk = 42;

fs::path utf8_path(const std::string &s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string utf8_string(const fs::path &p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Runs "parappa --write-test <dir>" and reports whether it created a file there and exited normally.
bool child_can_write(const fs::path &dir) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --write-test \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    DWORD code = 0;
    bool done = WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code);
    if (!done) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return done && code == kWriteTestOk;
}

}  // namespace

int write_test(const char *dir_utf8) {
    fs::path dir = utf8_path(dir_utf8);
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path probe = dir / "write_test.tmp";
    FILE *f = _wfopen(probe.wstring().c_str(), L"wb");
    if (!f) return 1;
    std::fclose(f);
    fs::remove(probe, ec);
    return kWriteTestOk;
}

fs::path choose_save_dir() {
    const char *base = SDL_GetBasePath();
    fs::path exe_dir = utf8_path(base ? base : ".");
    fs::path next_to_exe = exe_dir / "memcard" / "bu00";
    fs::path fallback = utf8_path(user_dir()) / "memcard" / "bu00";
    std::string key = utf8_string(exe_dir);
    bool ok;
    if (settings::get("saves_checked_for") == key) {
        ok = settings::get("saves_next_to_exe") == "1";
    } else {
        ok = child_can_write(next_to_exe);
        settings::set("saves_checked_for", key);
        settings::set("saves_next_to_exe", ok ? "1" : "0");
    }
    if (!ok)
        LOGW("Saves cannot be written next to the executable (blocked, e.g. by security software); using %s",
             utf8_string(fallback).c_str());
    return ok ? next_to_exe : fallback;
}

}  // namespace host
