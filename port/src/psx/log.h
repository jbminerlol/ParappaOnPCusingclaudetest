// Logging for the runtime. Messages go to stderr and parappa.log next to the executable.
#pragma once
#include <cstdarg>

namespace psx {

enum class LogLevel { Debug, Info, Warn, Error };

void log_init(const char *path);
void log_set_level(LogLevel level);
void logf(LogLevel level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
[[noreturn]] void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Logs the first call of an unimplemented / partially implemented library feature.
#define PSX_TODO_ONCE(...)                                     \
    do {                                                       \
        static bool once_;                                     \
        if (!once_) {                                          \
            once_ = true;                                      \
            ::psx::logf(::psx::LogLevel::Warn, __VA_ARGS__);   \
        }                                                      \
    } while (0)

}  // namespace psx

#define LOGD(...) ::psx::logf(::psx::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::psx::logf(::psx::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::psx::logf(::psx::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::psx::logf(::psx::LogLevel::Error, __VA_ARGS__)
