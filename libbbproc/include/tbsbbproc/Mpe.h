#pragma once

#include "Gse.h"
#include "SectionCollector.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace tbs {
namespace bb {

struct TBSBBPROC_API MpeStats
{
    uint64_t sections = 0;     // DSM-CC datagram sections (table 0x3E)
    uint64_t crcErrors = 0;
    uint64_t datagrams = 0;    // delivered
    uint64_t llcSnap = 0;      // datagrams with an LLC/SNAP header
    uint64_t scrambled = 0;    // payload scrambled: not decodable
    uint64_t invalid = 0;      // malformed sections / datagrams
    std::vector<uint16_t> pids; // PIDs carrying MPE
};

// Multi-Protocol Encapsulation (EN 301 192): IP datagrams in DSM-CC sections of a
// transport stream. PIDs are detected automatically from the first section with
// table id 0x3E. The destination MAC address is reported as a 6-byte label, so the
// output plugs into IpStatistics / PcapWriter like GSE PDUs.
class TBSBBPROC_API MpeDecapsulator
{
public:
    using PduCallback = std::function<void(const GsePdu &)>;

    void pushPackets(const uint8_t *packets, size_t count, const PduCallback &cb);
    void reset();
    MpeStats stats() const;

    // Decodes one complete section (exposed for tests and fuzzing)
    void pushSection(const uint8_t *s, size_t len, const PduCallback &cb);

private:
    std::map<uint16_t, SectionCollector> m_pids;
    MpeStats m_stats;
};

} // namespace bb
} // namespace tbs
