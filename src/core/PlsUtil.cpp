#include "PlsUtil.h"

namespace PlsUtil {

namespace {
// x sequence: x(i+18) = x(i+7) + x(i). The register is held with x(i) in bit 0.
inline uint32_t step(uint32_t x)
{
    const uint32_t fb = ((x >> 7) ^ x) & 1u;
    return (x >> 1) | (fb << 17);
}
constexpr uint32_t kPeriod = (1u << 18) - 1u;
} // namespace

uint32_t goldToRoot(uint32_t gold)
{
    uint32_t x = 1;
    for (uint32_t i = 0; i < gold % kPeriod; ++i)
        x = step(x);
    return x;
}

uint32_t rootToGold(uint32_t root)
{
    root &= kPeriod;
    uint32_t x = 1;
    for (uint32_t n = 0; n < kPeriod; ++n) {
        if (x == root)
            return n;
        x = step(x);
    }
    return 0;
}

} // namespace PlsUtil
