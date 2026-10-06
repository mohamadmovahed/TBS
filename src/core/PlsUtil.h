#pragma once

#include <cstdint>

// DVB-S2 physical layer scrambling code helpers (EN 302 307-1, 5.5.4).
namespace PlsUtil {

// Converts a gold code index n (0..262141) to the corresponding root code
// (the initial state of the x sequence register).
uint32_t goldToRoot(uint32_t gold);

// Converts a root code back to the gold code index. Returns 0 if the root
// code is not reachable (invalid input).
uint32_t rootToGold(uint32_t root);

} // namespace PlsUtil
