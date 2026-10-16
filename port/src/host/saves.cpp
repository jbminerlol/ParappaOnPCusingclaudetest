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

namespace {

// Saves made in the user folder while the game folder was blocked move along once it is allowed.
void copy_saves(const fs::path &from, const fs::path &to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return;
    if (fs::exists(to, ec) && !fs::is_empty(to, ec)) return;  // the game folder has its own saves
    int n = 0;
    for (const fs::directory_entry &e : fs::directory_iterator(from, ec)) {
        if (!e.is_regular_file(ec)) continue;
        fs::create_directories(to, ec);
        if (fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::skip_existing, ec)) n++;
    }
    if (n) LOGI("Copied %d save(s) from %s", n, utf8_string(from).c_str());
}

}  // namespace

fs::path choose_save_dir() {
    const char *base = SDL_GetBasePath();
    fs::path exe_dir = utf8_path(base ? base : ".");
    fs::path next_to_exe = exe_dir / "memcard" / "bu00";
    fs::path fallback = utf8_path(user_dir()) / "memcard" / "bu00";
    std::string key = utf8_string(exe_dir);
    // Checked once per game folder: each failed check is a write the security software blocks, and
    // repeated ones can get the program itself quarantined.
    bool ok;
    if (settings::get("saves_checked_for") == key) {
        ok = settings::get("saves_next_to_exe") == "1";
    } else {
        ok = child_can_write(next_to_exe);
        settings::set("saves_checked_for", key);
        settings::set("saves_next_to_exe", ok ? "1" : "0");
    }
    if (ok) {
        copy_saves(fallback, next_to_exe);
        return next_to_exe;
    }
    LOGW("Saves cannot be written next to the executable (blocked, e.g. by security software); using %s",
         utf8_string(fallback).c_str());
    // Tell the player once per game folder, so an empty memcard folder there is not taken for lost saves.
    if (settings::get("saves_notice_for") != key && !SDL_getenv("PARAPPA_AUTOINPUT")) {
        settings::set("saves_notice_for", key);
        info_box("PaRappa the Rapper - saves",
                 "Your saves are kept in:\n" + utf8_string(fallback) +
                     "\n\nSecurity software on this PC (for example Trend Micro Folder Shield) does not let "
                     "parappa.exe create files in the game folder, so the saves cannot go to the memcard "
                     "folder next to parappa.exe. Saving and loading work as usual.\n\n"
                     "To keep the saves in the game folder instead, allow parappa.exe in the security "
                     "software and delete the line starting with saves_checked_for from\n" +
                     utf8_string(utf8_path(user_dir()) / "settings.ini") +
                     "\nYour saves are then copied there automatically.\n\n"
                     "\"Open Saves Folder.bat\" in the game folder opens the folder with your saves.");
    }
    return fallback;
}

}  // namespace host
