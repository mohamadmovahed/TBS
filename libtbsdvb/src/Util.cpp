#include "Util.h"

#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace tbs {
namespace util {

#if defined(_WIN32)
static std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
#endif

std::FILE *openFile(const std::string &utf8Path, const char *mode)
{
#if defined(_WIN32)
    return _wfopen(widen(utf8Path).c_str(), widen(mode).c_str());
#else
    return std::fopen(utf8Path.c_str(), mode);
#endif
}

std::string homeDirectory()
{
#if defined(_WIN32)
    if (const char *p = std::getenv("USERPROFILE"))
        return p;
#endif
    if (const char *p = std::getenv("HOME"))
        return p;
    return ".";
}

} // namespace util
} // namespace tbs
