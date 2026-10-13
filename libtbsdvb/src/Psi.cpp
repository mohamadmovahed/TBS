#include "tbsdvb/Psi.h"

namespace tbs {

uint32_t crc32Mpeg(const uint8_t *p, size_t len, uint32_t crc)
{
    static const auto table = [] {
        struct T { uint32_t v[256]; } t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i << 24;
            for (int b = 0; b < 8; ++b)
                c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
            t.v[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < len; ++i)
        crc = (crc << 8) ^ table.v[((crc >> 24) ^ p[i]) & 0xFF];
    return crc;
}

} // namespace tbs
