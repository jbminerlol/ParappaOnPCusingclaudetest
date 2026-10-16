// libapi / BIOS services: events and the memory card (libcard + the "buXX:" file device).
//
// Memory card slot 1 ("bu00:") is a directory of save files (each a multiple of 8 KB blocks, as on
// the card): memcard\bu00 next to the executable, or in the user folder (host::choose_save_dir).
// Slot 2 reports no card.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "psx/log.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t CLASS_SWCARD = 0xF4000001u, CLASS_HWCARD = 0xF0000011u;
constexpr uint32_t SPEC_IOE = 0x0004, SPEC_ERROR = 0x8000, SPEC_TIMEOUT = 0x0100;
constexpr int BLOCK = 8192, CARD_BLOCKS = 15;

struct Event {
    uint32_t cls = 0, spec = 0;
    bool open = false, enabled = false, fired = false;
};
std::vector<Event> g_events;

fs::path g_card_dir;  // set at start-up (host::choose_save_dir)

// The memory card folder; saves made by earlier versions (in the user folder) are copied into a new
// one the first time.
fs::path card_dir() {
    static bool ready;
    if (!ready) {
        ready = true;
        char *pref = SDL_GetPrefPath("PaRappaPort", "memcard");
        fs::path old = fs::path(std::u8string(reinterpret_cast<const char8_t *>(pref ? pref : "."))) / "bu00";
        SDL_free(pref);
        if (g_card_dir.empty()) g_card_dir = old;
        std::error_code ec;
        fs::create_directories(g_card_dir, ec);
        if (g_card_dir != old && fs::is_empty(g_card_dir, ec) && fs::is_directory(old, ec)) {
            for (const auto &e : fs::directory_iterator(old, ec))
                if (e.is_regular_file())
                    fs::copy_file(e.path(), g_card_dir / e.path().filename(), fs::copy_options::skip_existing, ec);
        }
        LOGI("Memory card: %s", reinterpret_cast<const char *>(g_card_dir.u8string().c_str()));
    }
    return g_card_dir;
}

FILE *open_file(const fs::path &p, const wchar_t *mode) {
    return _wfopen(p.wstring().c_str(), mode);
}

void fire(uint32_t cls, uint32_t spec) {
    for (Event &e : g_events)
        if (e.open && e.cls == cls && e.spec == spec) e.fired = true;
}

// "bu00:NAME" -> (slot, NAME). Slot 0 is the only card present.
bool parse_device(const char *path, int *slot, std::string *name) {
    if (std::strncmp(path, "bu", 2) != 0 || !path[2] || !path[3] || path[4] != ':') return false;
    *slot = (path[2] - '0') * 2 + (path[3] - '0');
    *name = path + 5;
    return true;
}

struct OpenFile {
    FILE *f = nullptr;
    bool async = false;
};
std::vector<OpenFile> g_files(16);

struct DirEntry {  // BIOS DIRENTRY
    char name[20];
    int32_t attr;
    int32_t size;
    uint32_t next;
    int32_t head;
    char system[4];
};
static_assert(sizeof(DirEntry) == 40, "DIRENTRY");

std::vector<std::string> g_dir_list;
size_t g_dir_pos;

bool match(const std::string &pattern, const std::string &name) {
    // '*' matches the rest, '?' any character (BIOS semantics).
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '*') return true;
        if (i >= name.size() || (pattern[i] != '?' && pattern[i] != name[i])) return false;
    }
    return pattern.size() == name.size();
}

DirEntry *fill_dir(DirEntry *d) {
    if (g_dir_pos >= g_dir_list.size()) return nullptr;
    const std::string &n = g_dir_list[g_dir_pos++];
    std::memset(d, 0, sizeof(*d));
    std::strncpy(d->name, n.c_str(), sizeof(d->name) - 1);
    std::error_code ec;
    d->size = int32_t(fs::file_size(card_dir() / n, ec));
    d->attr = 0x50;
    return d;
}

int blocks_used() {
    int used = 0;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(card_dir(), ec))
        if (e.is_regular_file()) used += int((e.file_size() + BLOCK - 1) / BLOCK);
    return used;
}

}  // namespace

namespace card {

// Folder of memory card slot 1 (call before the game starts).
void set_dir(const std::filesystem::path &dir) {
    g_card_dir = dir;
}

}  // namespace card

extern "C" {

uint32_t psyq_OpenEvent(uint32_t cls, uint32_t spec, int mode, uint32_t func) {
    (void)mode;
    if (func) PSX_TODO_ONCE("OpenEvent with a handler function is not supported");
    g_events.push_back(Event{cls, spec, true, false, false});
    return 0xF1000000u | uint32_t(g_events.size() - 1);
}

int psyq_CloseEvent(uint32_t ev) {
    size_t i = ev & 0xFFFF;
    if (i < g_events.size()) g_events[i] = Event{};
    return 1;
}

int psyq_EnableEvent(uint32_t ev) {
    size_t i = ev & 0xFFFF;
    if (i < g_events.size()) g_events[i].enabled = true;
    return 1;
}

int psyq_TestEvent(uint32_t ev) {
    size_t i = ev & 0xFFFF;
    if (i >= g_events.size() || !g_events[i].fired) return 0;
    g_events[i].fired = false;
    return 1;
}

int psyq_InitCARD(int) { return 0; }
int psyq_StartCARD() { return 1; }
int psyq__bu_init() { return 0; }

// Card status check: slot 0 is present and formatted.
int psyq__card_info(int chan) {
    fire(CLASS_SWCARD, chan == 0 ? SPEC_IOE : SPEC_TIMEOUT);
    return 1;
}

int psyq__card_clear(int chan) {
    fire(CLASS_HWCARD, chan == 0 ? SPEC_IOE : SPEC_TIMEOUT);
    return 1;
}

int psyq__card_load(int chan) {
    fire(CLASS_SWCARD, chan == 0 ? SPEC_IOE : SPEC_TIMEOUT);
    return 1;
}

int psyq_format(const char *dev) {
    int slot;
    std::string name;
    if (parse_device(dev, &slot, &name) && slot == 0) {
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(card_dir(), ec)) fs::remove(e.path(), ec);
        fire(CLASS_HWCARD, SPEC_IOE);
        return 1;
    }
    fire(CLASS_HWCARD, SPEC_TIMEOUT);
    return 0;
}

// BIOS open: FREAD 1, FWRITE 2, FCREAT 0x200 (size in blocks in the upper 16 bits), FASYNC 0x8000.
int psyq_open(const char *path, uint32_t mode) {
    int slot;
    std::string name;
    if (!parse_device(path, &slot, &name) || slot != 0 || name.empty()) return -1;
    fs::path p = card_dir() / name;
    if (mode & 0x200) {
        int blocks = int(mode >> 16);
        if (fs::exists(p) || blocks_used() + blocks > CARD_BLOCKS) return -1;
        FILE *f = open_file(p, L"wb");
        if (!f) return -1;
        std::vector<uint8_t> zero(size_t(blocks) * BLOCK, 0);
        std::fwrite(zero.data(), 1, zero.size(), f);
        std::fclose(f);
        LOGI("Memory card: created %s (%d blocks)", name.c_str(), blocks);
    }
    FILE *f = open_file(p, (mode & 0x202) ? L"r+b" : L"rb");
    if (!f) return -1;
    LOGD("Memory card: open %s for %s", name.c_str(), (mode & 0x202) ? "writing" : "reading");
    for (size_t fd = 0; fd < g_files.size(); fd++) {
        if (!g_files[fd].f) {
            g_files[fd] = OpenFile{f, (mode & 0x8000) != 0};
            return int(fd);
        }
    }
    std::fclose(f);
    return -1;
}

int psyq_read(int fd, void *buf, int n) {
    if (fd < 0 || fd >= int(g_files.size()) || !g_files[fd].f) return -1;
    int got = int(std::fread(buf, 1, size_t(n), g_files[fd].f));
    if (g_files[fd].async) fire(CLASS_SWCARD, got == n ? SPEC_IOE : SPEC_ERROR);
    return got;
}

int psyq_write(int fd, const void *buf, int n) {
    if (fd < 0 || fd >= int(g_files.size()) || !g_files[fd].f) return -1;
    int put = int(std::fwrite(buf, 1, size_t(n), g_files[fd].f));
    std::fflush(g_files[fd].f);
    if (g_files[fd].async) fire(CLASS_SWCARD, put == n ? SPEC_IOE : SPEC_ERROR);
    return put;
}

int psyq_close(int fd) {
    if (fd < 0 || fd >= int(g_files.size()) || !g_files[fd].f) return -1;
    std::fclose(g_files[fd].f);
    g_files[fd] = OpenFile{};
    return fd;
}

int psyq_delete(const char *path) {
    int slot;
    std::string name;
    if (!parse_device(path, &slot, &name) || slot != 0) return 0;
    std::error_code ec;
    return fs::remove(card_dir() / name, ec) ? 1 : 0;
}

void *psyq_firstfile(const char *pattern, DirEntry *dir) {
    int slot;
    std::string pat;
    g_dir_list.clear();
    g_dir_pos = 0;
    if (!parse_device(pattern, &slot, &pat) || slot != 0) return nullptr;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(card_dir(), ec)) {
        std::string n = e.path().filename().string();
        if (e.is_regular_file() && match(pat, n)) g_dir_list.push_back(n);
    }
    LOGD("Memory card: firstfile %s -> %zu file(s)", pattern, g_dir_list.size());
    return fill_dir(dir);
}

void *psyq_nextfile(DirEntry *dir) {
    return fill_dir(dir);
}

}  // extern "C"
