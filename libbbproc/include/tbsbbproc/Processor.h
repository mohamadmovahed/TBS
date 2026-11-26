#pragma once

#include "GenericDecoders.h"
#include "Gse.h"
#include "IpStatistics.h"
#include "Mpe.h"
#include "TunWriter.h"
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
    TsInGenericStream, // non-standard: TS packets (plain or CRC-8 sync) in a generic stream
    RawIp,             // non-standard: IPv4 packets directly in the data field
};

TBSBBPROC_API const char *streamContentName(StreamContent c);
// TransportStream, TsInput or TsInGenericStream: StreamReport::ts is filled in
TBSBBPROC_API bool isTransportStream(StreamContent c);

struct TBSBBPROC_API ProcessorConfig
{
    bool analyzeTables = true;
    bool decodeGse = true;     // decode generic streams at all (GSE and the variants below)
    // Generic streams (TS/GS = 00, 01, 10). Auto probes standard GSE and the
    // non-standard variants on the first probeFrames data fields of each stream and
    // keeps the one that explains most of the data; it falls back to standard GSE
    // (generic packetized streams: to counting only) when nothing fits.
    GenericDecoder genericDecoder = GenericDecoder::Auto;
    unsigned probeFrames = 64;
    // Some modulators append a CRC-32 to each BBFrame data field. When it matches,
    // the 4 bytes are removed before decoding (generic streams only).
    bool stripFrameCrc32 = true;
    bool decodeMpe = true;     // IP in MPE sections of transport streams
    PcapOptions pcap;
    TunOptions tun;            // Linux TUN/TAP output of the decapsulated packets
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
    MpeStats mpe;                // MPE layer (transport streams)
    IpReport ip;                 // IP statistics (GSE, MPE or raw IP)

    // Generic streams: the decoder in use. Auto while detection is still running.
    GenericDecoder decoder = GenericDecoder::None;
    bool decoderDetected = false;          // chosen by auto detection
    std::vector<DecoderScore> decoderScores; // auto detection result, best first
    uint64_t frameCrc32 = 0;               // frames with a trailing CRC-32 (removed)
    TsExtractStats tsExtract;              // TsInGenericStream
    RawIpStats rawIp;                      // RawIp
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
    bool tunOpen = false;
    std::string tunName;
    uint64_t tunPackets = 0;
    uint64_t tunErrors = 0;
    std::string tunError;
    std::string tunWarning;
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

    // Opens the pcap and TUN/TAP outputs if enabled
    bool open(std::string *error = nullptr);
    // Ends the input: decides decoders still being probed and processes what they held.
    // Also called by close().
    void finish();
    void close();

    // One BBFrame: BBHeader + data field
    void pushBbFrame(const uint8_t *frame, size_t len, const tbs::BbHeader &hdr);
    // Aligned TS packets of a plain transport stream capture
    void pushTs(const uint8_t *packets, size_t count);

    // Rates are computed relative to the previous snapshot() call; also flushes the pcap file
    ProcessorSnapshot snapshot();
    // EPG of the transport stream on this input stream (-1 = single stream / TS capture).
    // Can be large: fetch it when StreamReport::ts.epgVersion changes.
    std::vector<ServiceSchedule> epg(int isi);
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Human readable multi-line summary (used by the command line tools)
TBSBBPROC_API std::string formatReport(const ProcessorSnapshot &s, bool verbose = false);

} // namespace bb
} // namespace tbs
