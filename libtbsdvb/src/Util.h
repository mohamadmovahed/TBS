#pragma once

// Internal helpers (not part of the public API).

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>

namespace tbs {
namespace util {

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string format(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return {};
    if (static_cast<size_t>(n) < sizeof(buf))
        return std::string(buf, static_cast<size_t>(n));
    std::string out(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(&out[0], out.size(), fmt, ap);
    va_end(ap);
    out.resize(static_cast<size_t>(n));
    return out;
}

inline void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// fopen() that accepts UTF-8 paths on every platform
std::FILE *openFile(const std::string &utf8Path, const char *mode);

std::string homeDirectory();

// Monotonic milliseconds
inline int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace util
} // namespace tbs
