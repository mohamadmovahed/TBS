#pragma once

#include "PsiTypes.h"
#include "SectionCollector.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace tbs {
namespace bb {

// Collects PSI/SI sections from a transport stream and decodes PAT, CAT,
// PMT, NIT, SDT, BAT, EIT (present/following; schedule counted) and TDT/TOT.
// Not thread safe: feed and query from one thread (Processor adds locking).
class TBSBBPROC_API TsTableAnalyzer
{
public:
    TsTableAnalyzer();
    ~TsTableAnalyzer();
    TsTableAnalyzer(const TsTableAnalyzer &) = delete;
    TsTableAnalyzer &operator=(const TsTableAnalyzer &) = delete;

    // Aligned 188-byte packets
    void pushPackets(const uint8_t *packets, size_t count);
    // One complete section (table_id .. CRC); also used by the tests
    void pushSection(uint16_t pid, const uint8_t *section, size_t len);
    void reset();

    TsReport report() const;
    uint64_t packets() const { return m_packets; }

private:
    struct TableKey
    {
        uint8_t tableId;
        uint16_t pid;
        bool operator<(const TableKey &o) const { return tableId != o.tableId ? tableId < o.tableId : pid < o.pid; }
    };

    bool wantsPid(uint16_t pid) const;
    void onSection(uint16_t pid, const uint8_t *s, size_t len);
    void parsePat(const uint8_t *s, size_t len, int version);
    void parseCat(const uint8_t *s, size_t len);
    void parsePmt(uint16_t pid, const uint8_t *s, size_t len, int version);
    void parseNitBat(const uint8_t *s, size_t len, bool bat, int version);
    void parseSdt(const uint8_t *s, size_t len);
    void parseEit(const uint8_t *s, size_t len);
    void parseTime(const uint8_t *s, size_t len);
    void rebuildPidUsage(TsReport &r) const;

    uint64_t m_packets = 0;
    std::map<uint16_t, SectionCollector> m_collectors;
    std::set<uint16_t> m_pmtPids;

    // Decoded state
    bool m_patSeen = false;
    uint16_t m_tsid = 0;
    int m_patVersion = -1;
    std::map<uint16_t, ProgramInfo> m_programs;
    std::vector<CaInfo> m_emm;
    std::map<std::pair<bool, uint16_t>, NetworkInfo> m_networks; // (other?, network id)
    std::map<uint16_t, BouquetInfo> m_bouquets;
    // key: onid << 32 | tsid << 16 | service id
    std::map<uint64_t, ServiceInfo> m_services;
    uint16_t m_onid = 0;
    bool m_hasTime = false;
    int64_t m_utc = 0;
    uint64_t m_sections = 0;
    uint64_t m_crcErrors = 0;
    uint64_t m_scheduleEvents = 0;
    std::map<TableKey, TableStat> m_tableStats;
    std::map<TableKey, std::set<uint32_t>> m_tableInstances;
    std::set<uint64_t> m_scheduleSeen; // (service << 16 | event id) to count unique schedule events
};

} // namespace bb
} // namespace tbs
