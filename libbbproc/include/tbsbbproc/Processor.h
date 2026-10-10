#pragma once

#include "Gse.h"
#include "IpStatistics.h"
#include "PcapWriter.h"
#include "PsiTypes.h"
#include "export.h"

#include <tbsdvb/BbHeader.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

// What a stream (one ISI of the carrier) carries
enum class StreamContent {
    Unknown = 0,
    TransportStream,   // MATYPE TS/GS = 11: TS packets recovered from the BBFrames
    Gse,               // generic continuous / GSE-HEM, data fields parse as GSE
    GenericContinuous, // generic continuous but not GSE
    GenericPacketized, // TS/GS = 00, UPL != 0
    TsInput,           // the capture was a plain transport stream (no BBFrames)
};

TBSBBPROC_API const char *streamContentName(StreamContent c);

struct TBSBBPROC_API ProcessorConfig
{
    bool analyzeTables = true;
    bool decodeGse = true;
    PcapOptions pcap;
    size_t maxFlows = 20000;  // per stream
    size_t topFlows = 100;    // flows returned in a report
};

struct TBSBBPROC_API StreamReport
{
    int isi = -1;                // -1 = single input stream
    StreamContent content = StreamContent::Unknown;
    uint64_t frames = 0;         // BBFrames
    uint64_t dataBytes = 0;      // data field bytes
    double mbps = 0.0;           // data field rate since the previous snapshot
    bool hasHeader = false;
    tbs::BbHeader lastHeader;
    uint64_t tsPackets = 0;      // TS packets (TS streams)
    TsReport ts;                 // PSI/SI analysis (TS streams)
    GseStats gse;                // GSE layer (generic streams)
    IpReport ip;                 // IP statistics (GSE streams)
};

struct TBSBBPROC_API ProcessorSnapshot
{
    double elapsedSec = 0.0;
    std::vector<StreamReport> streams; // sorted by ISI
    bool pcapOpen = false;
    std::string pcapFile;
    uint64_t pcapPackets = 0;
    uint64_t pcapBytes = 0;
    std::string pcapError;
};

// BBFrame / TS processing pipeline. Thread safe: push*() may run on a capture
// thread while snapshot() is called from another (e.g. a GUI timer).
class TBSBBPROC_API Processor
{
public:
    explicit Processor(const ProcessorConfig &cfg = ProcessorConfig());
    ~Processor();
    Processor(const Processor &) = delete;
    Processor &operator=(const Processor &) = delete;

    // Opens the pcap output if enabled
    bool open(std::string *error = nullptr);
    void close();

    // One BBFrame: BBHeader + data field
    void pushBbFrame(const uint8_t *frame, size_t len, const tbs::BbHeader &hdr);
    // Aligned TS packets of a plain transport stream capture
    void pushTs(const uint8_t *packets, size_t count);

    // Rates are computed relative to the previous snapshot() call; also flushes the pcap file
    ProcessorSnapshot snapshot();
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Human readable multi-line summary (used by the command line tools)
TBSBBPROC_API std::string formatReport(const ProcessorSnapshot &s, bool verbose = false);

} // namespace bb
} // namespace tbs
