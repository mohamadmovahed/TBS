#pragma once

#include "Psi.h"
#include "export.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace tbs {

struct TBSDVB_API TsFilterOptions
{
    int programNumber = 0;      // > 0: keep this service only and rewrite the PAT (SPTS)
    std::vector<uint16_t> pids; // extra PIDs; without a program: keep only these
    bool dropNullPackets = true;
};

// Selects packets from a transport stream: everything, a PID list, or one service.
// For a service the PAT and PMT are followed, so PID changes are picked up; the output
// contains a PAT listing only that service, its PMT, PCR, elementary stream and ECM PIDs.
class TBSDVB_API TsFilter
{
public:
    using PacketCallback = std::function<void(const uint8_t *pkt188)>;

    explicit TsFilter(const TsFilterOptions &opt = {});

    void push(const uint8_t *pkt, const PacketCallback &out);
    void reset();

    // Service mode: whether the service is in the PAT / its PMT has been seen
    bool programFound() const { return m_pmtPid >= 0; }
    bool pmtSeen() const { return m_pmtSeen; }
    int pmtPid() const { return m_pmtPid; }
    // PIDs passed through in service mode (PMT, PCR, ES, ECM, extras), sorted
    std::vector<uint16_t> servicePids() const;

    uint64_t packetsIn() const { return m_in; }
    uint64_t packetsOut() const { return m_out; }

private:
    void onPat(const uint8_t *s, size_t len);
    void onPmt(const uint8_t *s, size_t len);
    void buildPat();
    void emitPat(const PacketCallback &out);

    TsFilterOptions m_opt;
    std::bitset<8192> m_pass;  // PIDs passed unchanged
    std::bitset<8192> m_extra; // from m_opt.pids
    SectionCollector m_patCollector;
    SectionCollector m_pmtCollector;
    int m_pmtPid = -1;
    bool m_pmtSeen = false;
    int m_tsid = -1;
    std::vector<uint16_t> m_serviceSet;
    std::vector<uint8_t> m_pat; // our single-program PAT section
    uint8_t m_patVersion = 0;
    uint8_t m_patCc = 0;
    uint64_t m_in = 0;
    uint64_t m_out = 0;
};

} // namespace tbs
