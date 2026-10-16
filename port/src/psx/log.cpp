#include "psx/log.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

namespace psx {

static FILE *g_log;
static LogLevel g_level = LogLevel::Info;

void log_init(const char *path) {
    g_log = std::fopen(path, "w");
}

void log_set_level(LogLevel level) {
    g_level = level;
}

static void vlog(LogLevel level, const char *fmt, va_list ap) {
    static const char *const tags[] = {"debug", "info", "warn", "error"};
    char msg[2048];
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    double t = SDL_GetTicksNS() / 1e9;
    std::fprintf(stderr, "[%9.3f %s] %s\n", t, tags[int(level)], msg);
    if (g_log) {
        std::fprintf(g_log, "[%9.3f %s] %s\n", t, tags[int(level)], msg);
        std::fflush(g_log);
    }
}

void logf(LogLevel level, const char *fmt, ...) {
    if (level < g_level) return;
    va_list ap;
    va_start(ap, fmt);
    vlog(level, fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    logf(LogLevel::Error, "fatal: %s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PaRappa the Rapper", msg, nullptr);
    std::exit(1);
}

}  // namespace psx
