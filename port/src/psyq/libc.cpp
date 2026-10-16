// PsyQ libc subset and the GCC soft-float helpers the game calls (doubles travel as two words).
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "psx/log.h"
#include "psx/memory.h"

namespace {

uint32_t g_rand_next = 0x24040001;

double to_double(uint32_t lo, uint32_t hi) {
    uint64_t bits = (uint64_t(hi) << 32) | lo;
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

uint64_t from_double(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    return bits;
}

}  // namespace

extern "C" {

int psyq_rand() {
    g_rand_next = g_rand_next * 1103515245u + 12345u;
    return int((g_rand_next >> 16) & 0x7FFF);
}

int psyq_srand(uint32_t seed) {
    g_rand_next = seed;
    return 0;
}

int psyq_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int psyq_strcmp(const char *a, const char *b) { return std::strcmp(psx::mirror(a), psx::mirror(b)); }
int psyq_strncmp(const char *a, const char *b, int n) { return std::strncmp(psx::mirror(a), psx::mirror(b), size_t(n)); }
char *psyq_strcpy(char *d, const char *s) { std::strcpy(psx::mirror(d), psx::mirror(s)); return d; }
char *psyq_strcat(char *d, const char *s) { std::strcat(psx::mirror(d), psx::mirror(s)); return d; }
int psyq_strlen(const char *s) { return int(std::strlen(psx::mirror(s))); }
int psyq_bcmp(const void *a, const void *b, int n) { return std::memcmp(psx::mirror(a), psx::mirror(b), size_t(n)) != 0; }

int psyq_exit(int code) {
    LOGI("Game called exit(%d)", code);
    std::exit(code);
}

uint64_t psyq___adddf3(uint32_t alo, uint32_t ahi, uint32_t blo, uint32_t bhi) {
    return from_double(to_double(alo, ahi) + to_double(blo, bhi));
}

uint64_t psyq___divdf3(uint32_t alo, uint32_t ahi, uint32_t blo, uint32_t bhi) {
    return from_double(to_double(alo, ahi) / to_double(blo, bhi));
}

int psyq___fixdfsi(uint32_t lo, uint32_t hi) {
    return int(to_double(lo, hi));
}

uint64_t psyq___floatsidf(int v) {
    return from_double(double(v));
}

}  // extern "C"
