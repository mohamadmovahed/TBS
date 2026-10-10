#pragma once

#include "export.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace tbs {
namespace bb {

// One PDU delivered by the GSE layer (ETSI TS 102 606-1)
struct TBSBBPROC_API GsePdu
{
    uint16_t protocolType = 0; // EtherType after extension headers (0x0800 IPv4, 0x86DD IPv6, ...)
    bool bridged = false;      // data is a complete Ethernet frame (Bridged-SNDU extension)
    const uint8_t *data = nullptr;
    size_t len = 0;
    uint8_t labelType = 0;     // 0 = 6 byte, 1 = 3 byte, 2 = broadcast, 3 = re-use
    uint8_t label[6] = {};
    uint8_t labelLen = 0;
};

struct TBSBBPROC_API GseStats
{
    uint64_t dataFields = 0;      // BBFrame data fields processed
    uint64_t validDataFields = 0; // parsed cleanly as GSE
    uint64_t invalidDataFields = 0;
    uint64_t packets = 0;         // GSE packets
    uint64_t completePdus = 0;    // S=1, E=1
    uint64_t fragments = 0;       // GSE packets carrying a fragment
    uint64_t reassembled = 0;     // PDUs rebuilt from fragments
    uint64_t crcErrors = 0;       // reassembly CRC-32 failures
    uint64_t lengthErrors = 0;    // malformed GSE / total length mismatch
    uint64_t fragmentErrors = 0;  // missing first fragment, timeout, overflow
    uint64_t paddingBytes = 0;
    uint64_t pdus = 0;            // PDUs delivered
    uint64_t pduBytes = 0;
    uint64_t testPdus = 0;        // Test SNDU extension (discarded)
    uint64_t unsupportedExtensions = 0;
    std::array<uint64_t, 4> labelTypes{}; // per label type
    std::map<uint16_t, uint64_t> protocolTypes; // EtherType -> PDUs
};

// GSE decapsulator for one stream. In normal mode (TS/GS = 01) every data field
// holds whole GSE packets; in GSE-HEM (TS/GS = 10) packets may continue in the
// next BBFrame and SYNCD gives the offset of the first packet start.
class TBSBBPROC_API GseDecapsulator
{
public:
    using PduCallback = std::function<void(const GsePdu &)>;

    void pushDataField(const uint8_t *df, size_t len, bool hem, uint16_t syncdBits, const PduCallback &cb);
    void reset();
    const GseStats &stats() const { return m_stats; }

    // Fragments older than this many data fields are discarded
    static constexpr uint64_t kFragmentTimeout = 256;

private:
    struct Fragment
    {
        bool active = false;
        uint8_t labelType = 0;
        uint64_t startField = 0;
        std::vector<uint8_t> data; // from the Total_Length field to the end of the PDU
    };

    bool parsePackets(const uint8_t *p, size_t len, bool allowTail, std::vector<uint8_t> *tail, const PduCallback &cb);
    bool handlePacket(const uint8_t *p, size_t len, const PduCallback &cb);
    void deliver(uint16_t type, uint8_t labelType, const uint8_t *label, const uint8_t *pdu, size_t len, const PduCallback &cb);

    GseStats m_stats;
    std::array<Fragment, 256> m_frags;
    uint64_t m_field = 0;
    uint8_t m_lastLabel[6] = {};
    uint8_t m_lastLabelLen = 0;
    bool m_haveLastLabel = false;
    std::vector<uint8_t> m_carry; // HEM: packet continuing in the next data field
    bool m_hemSynced = false;
};

} // namespace bb
} // namespace tbs
