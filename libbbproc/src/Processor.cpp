#include "tbsbbproc/Processor.h"

#include "tbsbbproc/DvbText.h"
#include "tbsbbproc/TsTableAnalyzer.h"

#include <tbsdvb/BbFrameToTs.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

namespace tbs {
namespace bb {

namespace {
int64_t steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int64_t unixMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr int kTsInputKey = -2;
constexpr int64_t kProbeMaxMs = 1500; // decide by then even if fewer frames arrived
constexpr int kMaxReprobes = 3;
constexpr uint64_t kReprobeFragmentErrors = 32;

inline uint32_t u32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// BBHeader + data field ending in a CRC-32/MPEG-2 over everything before it
// (seen with initial value 0 as well as the usual 0xFFFFFFFF)
bool hasTrailingCrc32(const uint8_t *frame, size_t len)
{
    if (len < tbs::BbHeader::kSize + 8)
        return false;
    const uint32_t want = u32(frame + len - 4);
    return crc32Mpeg(frame, len - 4, 0) == want || crc32Mpeg(frame, len - 4) == want;
}

enum PduKind { kGse = 0, kMpe = 1, kRawIp = 2 };
const char *pduKindName(int k)
{
    return k == kMpe ? "TS MPE" : k == kRawIp ? "IP" : "GSE";
}
} // namespace

const char *streamContentName(StreamContent c)
{
    switch (c) {
    case StreamContent::Unknown:
        return "Unknown";
    case StreamContent::TransportStream:
        return "Transport stream";
    case StreamContent::Gse:
        return "GSE (IP)";
    case StreamContent::GenericContinuous:
        return "Generic continuous (not GSE)";
    case StreamContent::GenericPacketized:
        return "Generic packetized";
    case StreamContent::TsInput:
        return "Transport stream";
    case StreamContent::TsInGenericStream:
        return "Transport stream in a generic stream (non-standard)";
    case StreamContent::RawIp:
        return "Raw IP (non-standard)";
    }
    return "";
}

bool isTransportStream(StreamContent c)
{
    return c == StreamContent::TransportStream || c == StreamContent::TsInput || c == StreamContent::TsInGenericStream;
}

struct Processor::Impl
{
    struct Stream
    {
        explicit Stream(size_t maxFlows) : ip(maxFlows) {}
        int key = -1;
        uint64_t frames = 0;
        uint64_t dataBytes = 0;
        uint64_t dataBytesAtSnapshot = 0;
        bool hasHeader = false;
        tbs::BbHeader header;
        bool sawTsType = false;
        bool sawGpType = false;
        bool sawGeneric = false;
        tbs::BbFrameToTs bb2ts;
        std::vector<uint8_t> tsBuf;
        TsTableAnalyzer psi;
        GseDecapsulator gse;
        MpeDecapsulator mpe;
        IpStatistics ip;

        // Generic streams
        GenericDecoder decoder = GenericDecoder::Auto; // Auto = probing
        bool decoderDetected = false;
        std::unique_ptr<GenericDecoderProbe> probe;
        struct HeldField
        {
            std::vector<uint8_t> data;
            bool hem;
            uint16_t syncd;
        };
        std::vector<HeldField> held; // data fields seen while probing, replayed afterwards
        int64_t probeStartMs = 0;
        std::vector<DecoderScore> scores;
        std::unique_ptr<GenericTsExtractor> tsx;
        std::unique_ptr<RawIpExtractor> rawIp;
        uint64_t frameCrc32 = 0;
        // Re-detection when an auto-detected GSE decoder keeps losing fragments
        // (the early decision saw too few fragmented PDUs to tell the dialects apart)
        int reprobes = 0;
        uint64_t fragErrorsBase = 0;
        uint64_t reassembledBase = 0;
    };

    ProcessorConfig cfg;
    std::mutex mutex;
    std::map<int, std::unique_ptr<Stream>> streams;
    std::unique_ptr<PcapWriter> pcap;
    std::string pcapError;
    std::unique_ptr<TunWriter> tun;
    std::string tunError;
    std::string tunWarning;
    int64_t startMs = steadyMs();
    int64_t lastSnapshotMs = 0;

    Stream &stream(int key)
    {
        auto &s = streams[key];
        if (!s) {
            s = std::make_unique<Stream>(cfg.maxFlows);
            s->key = key;
        }
        return *s;
    }

    void onPdu(Stream &s, const GsePdu &pdu, int kind)
    {
        s.ip.addPdu(pdu, steadyMs());
        if (pcap && pcap->isOpen()) {
            // pcapng: one interface per stream and encapsulation
            const int key = s.key * 4 + kind;
            std::string name;
            if (s.key >= 0)
                name = "ISI " + std::to_string(s.key) + " ";
            name += pduKindName(kind);
            pcap->write(pdu, unixMicros(), key, name);
            if (!pcap->lastError().empty() && pcapError.empty())
                pcapError = pcap->lastError();
        }
        if (tun && tun->isOpen())
            tun->write(pdu);
    }

    void pushTsToMpe(Stream &s, const uint8_t *packets, size_t count)
    {
        if (cfg.decodeMpe)
            s.mpe.pushPackets(packets, count, [this, &s](const GsePdu &pdu) { onPdu(s, pdu, kMpe); });
    }

    void pushTsPackets(Stream &s)
    {
        if (s.tsBuf.empty())
            return;
        if (cfg.analyzeTables)
            s.psi.pushPackets(s.tsBuf.data(), s.tsBuf.size() / 188);
        pushTsToMpe(s, s.tsBuf.data(), s.tsBuf.size() / 188);
    }

    // Selects the decoder of a generic stream and replays the held data fields
    void useDecoder(Stream &s, GenericDecoder dec, bool detected)
    {
        s.decoder = dec;
        s.decoderDetected = detected;
        s.fragErrorsBase = s.gse.stats().fragmentErrors;
        s.reassembledBase = s.gse.stats().reassembled;
        if (isGseDecoder(dec))
            s.gse.setDialect(gseDialectFor(dec));
        else if (dec == GenericDecoder::Ts || dec == GenericDecoder::CrcTs)
            s.tsx = std::make_unique<GenericTsExtractor>(dec == GenericDecoder::CrcTs);
        else if (dec == GenericDecoder::RawIp || dec == GenericDecoder::RawIpSwapped)
            s.rawIp = std::make_unique<RawIpExtractor>(dec == GenericDecoder::RawIpSwapped);
        std::vector<Stream::HeldField> held;
        held.swap(s.held);
        for (const auto &f : held)
            decodeGeneric(s, f.data.data(), f.data.size(), f.hem, f.syncd);
    }

    void decideProbe(Stream &s)
    {
        if (s.decoder != GenericDecoder::Auto || !s.probe)
            return;
        s.scores = s.probe->scores();
        GenericDecoder best = s.probe->best();
        const bool detected = best != GenericDecoder::None;
        if (!detected)
            best = s.sawGpType ? GenericDecoder::None : GenericDecoder::Gse; // what the standard says
        s.probe.reset();
        useDecoder(s, best, detected);
    }

    void decodeGeneric(Stream &s, const uint8_t *df, size_t len, bool hem, uint16_t syncd)
    {
        switch (s.decoder) {
        case GenericDecoder::Auto:
            if (!s.probe) {
                s.probe = std::make_unique<GenericDecoderProbe>();
                s.probeStartMs = steadyMs();
            }
            s.probe->pushDataField(df, len, hem, syncd);
            s.held.push_back({std::vector<uint8_t>(df, df + len), hem, syncd});
            if (s.probe->dataFields() >= std::max(1u, cfg.probeFrames) || s.probe->conclusive())
                decideProbe(s);
            break;
        case GenericDecoder::Gse:
        case GenericDecoder::GseLen2:
        case GenericDecoder::GseSplitFragId:
        case GenericDecoder::GseLen2SplitFragId:
            s.gse.pushDataField(df, len, hem, syncd, [this, &s](const GsePdu &pdu) { onPdu(s, pdu, kGse); });
            if (s.decoderDetected && s.reprobes < kMaxReprobes) {
                const GseStats &g = s.gse.stats();
                const uint64_t lost = g.fragmentErrors - s.fragErrorsBase;
                if (lost >= kReprobeFragmentErrors && lost > 2 * (g.reassembled - s.reassembledBase)) {
                    ++s.reprobes;
                    s.decoder = GenericDecoder::Auto; // probe again from the next data field
                    s.decoderDetected = false;
                }
            }
            break;
        case GenericDecoder::Ts:
        case GenericDecoder::CrcTs:
            s.tsBuf.clear();
            s.tsx->pushDataField(df, len, [&s](const uint8_t *pkt) { s.tsBuf.insert(s.tsBuf.end(), pkt, pkt + 188); });
            pushTsPackets(s);
            break;
        case GenericDecoder::RawIp:
        case GenericDecoder::RawIpSwapped:
            s.rawIp->pushDataField(df, len, [this, &s](const GsePdu &pdu) { onPdu(s, pdu, kRawIp); });
            break;
        case GenericDecoder::None:
            break;
        }
    }

    void finishProbes(bool force)
    {
        const int64_t now = steadyMs();
        for (auto &kv : streams) {
            Stream &s = *kv.second;
            if (s.decoder == GenericDecoder::Auto && s.probe && (force || now - s.probeStartMs >= kProbeMaxMs))
                decideProbe(s);
        }
    }
};

Processor::Processor(const ProcessorConfig &cfg)
    : d(std::make_unique<Impl>())
{
    d->cfg = cfg;
}

Processor::~Processor()
{
    close();
}

bool Processor::open(std::string *error)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    bool ok = true;
    std::string errors;
    if (d->cfg.pcap.enabled) {
        d->pcap = std::make_unique<PcapWriter>(d->cfg.pcap);
        std::string e;
        if (!d->pcap->open(&e)) {
            d->pcapError = e;
            d->pcap.reset();
            errors = e;
            ok = false;
        }
    }
    if (d->cfg.tun.enabled) {
        d->tun = std::make_unique<TunWriter>(d->cfg.tun);
        std::string e;
        if (!d->tun->open(&e, &d->tunWarning)) {
            d->tunError = e;
            d->tun.reset();
            errors += (errors.empty() ? "" : "; ") + e;
            ok = false;
        }
    }
    if (!ok && error)
        *error = errors;
    return ok;
}

void Processor::finish()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->finishProbes(true);
}

void Processor::close()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->finishProbes(true);
    if (d->pcap)
        d->pcap->close();
    if (d->tun)
        d->tun->close();
}

void Processor::reset()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->streams.clear();
    d->startMs = steadyMs();
    d->lastSnapshotMs = 0;
}

void Processor::pushBbFrame(const uint8_t *frame, size_t len, const tbs::BbHeader &hdr)
{
    if (len < tbs::BbHeader::kSize || !hdr.valid())
        return;
    std::lock_guard<std::mutex> lock(d->mutex);
    Impl::Stream &s = d->stream(hdr.isi());
    ++s.frames;
    const uint8_t *df = frame + tbs::BbHeader::kSize;
    size_t dfLen = std::min(len - tbs::BbHeader::kSize, hdr.dataFieldBytes());
    s.dataBytes += dfLen;
    s.header = hdr;
    s.hasHeader = true;

    switch (hdr.streamType()) {
    case tbs::BbHeader::StreamType::Transport: {
        s.sawTsType = true;
        if (!d->cfg.analyzeTables && !d->cfg.decodeMpe)
            break;
        s.tsBuf.clear();
        s.bb2ts.pushFrame(frame, len, hdr, [&s](const uint8_t *pkt) { s.tsBuf.insert(s.tsBuf.end(), pkt, pkt + 188); });
        d->pushTsPackets(s);
        break;
    }
    case tbs::BbHeader::StreamType::GenericPacketized:
    case tbs::BbHeader::StreamType::GenericContinuous:
    case tbs::BbHeader::StreamType::GseHem: {
        const bool gp = hdr.streamType() == tbs::BbHeader::StreamType::GenericPacketized;
        if (gp)
            s.sawGpType = true;
        if (!d->cfg.decodeGse)
            break;
        if (d->cfg.stripFrameCrc32 && hasTrailingCrc32(frame, tbs::BbHeader::kSize + dfLen)) {
            dfLen -= 4;
            ++s.frameCrc32;
        }
        s.sawGeneric = true;
        if (s.decoder == GenericDecoder::Auto && d->cfg.genericDecoder != GenericDecoder::Auto)
            d->useDecoder(s, d->cfg.genericDecoder, false);
        // Generic packetized streams have no standard IP encapsulation: only
        // detected (or explicitly configured) decoders run on them
        const bool hem = hdr.streamType() == tbs::BbHeader::StreamType::GseHem;
        d->decodeGeneric(s, df, dfLen, hem, hdr.syncd);
        break;
    }
    }
}

void Processor::pushTs(const uint8_t *packets, size_t count)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    Impl::Stream &s = d->stream(kTsInputKey);
    s.dataBytes += count * 188;
    if (d->cfg.analyzeTables)
        s.psi.pushPackets(packets, count);
    d->pushTsToMpe(s, packets, count);
}

std::vector<ServiceSchedule> Processor::epg(int isi)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    auto it = d->streams.find(isi);
    if (it == d->streams.end() && isi < 0)
        it = d->streams.find(kTsInputKey); // plain TS capture
    if (it == d->streams.end())
        return {};
    return it->second->psi.epg();
}

ProcessorSnapshot Processor::snapshot()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->finishProbes(false);
    const int64_t now = steadyMs();
    const double dt = d->lastSnapshotMs > 0 ? (now - d->lastSnapshotMs) / 1000.0 : 0.0;
    d->lastSnapshotMs = now;

    ProcessorSnapshot snap;
    snap.elapsedSec = (now - d->startMs) / 1000.0;
    for (auto &kv : d->streams) {
        Impl::Stream &s = *kv.second;
        StreamReport r;
        r.isi = s.key == kTsInputKey ? -1 : s.key;
        r.frames = s.frames;
        r.dataBytes = s.dataBytes;
        if (dt > 0)
            r.mbps = (s.dataBytes - s.dataBytesAtSnapshot) * 8.0 / dt / 1e6;
        s.dataBytesAtSnapshot = s.dataBytes;
        r.hasHeader = s.hasHeader;
        r.lastHeader = s.header;
        r.gse = s.gse.stats();
        r.mpe = s.mpe.stats();
        r.tsPackets = s.psi.packets();

        r.decoder = s.sawGeneric ? s.decoder : GenericDecoder::None;
        r.decoderDetected = s.decoderDetected;
        r.decoderScores = s.scores;
        r.frameCrc32 = s.frameCrc32;
        if (s.tsx)
            r.tsExtract = s.tsx->stats();
        if (s.rawIp)
            r.rawIp = s.rawIp->stats();
        const bool tsDecoder = s.decoder == GenericDecoder::Ts || s.decoder == GenericDecoder::CrcTs;
        const bool ipDecoder = s.decoder == GenericDecoder::RawIp || s.decoder == GenericDecoder::RawIpSwapped;

        if (s.key == kTsInputKey) {
            r.content = StreamContent::TsInput;
        } else if (s.sawTsType) {
            r.content = StreamContent::TransportStream;
        } else if (tsDecoder && r.tsExtract.verified > 0) {
            r.content = StreamContent::TsInGenericStream;
        } else if (ipDecoder && r.rawIp.packets > 0) {
            r.content = StreamContent::RawIp;
        } else if (s.sawGpType && !(isGseDecoder(s.decoder) && r.gse.pdus > 0)) {
            r.content = StreamContent::GenericPacketized;
        } else if (r.gse.dataFields > 0) {
            // GSE if the large majority of data fields parse cleanly and PDUs come out
            const bool gse = r.gse.validDataFields * 10 >= r.gse.dataFields * 8 && (r.gse.packets > 0 || r.gse.paddingBytes > 0);
            r.content = gse ? StreamContent::Gse : StreamContent::GenericContinuous;
        }
        if (isTransportStream(r.content))
            r.ts = s.psi.report();
        if (r.content == StreamContent::Gse || r.gse.pdus > 0 || r.mpe.datagrams > 0 || r.rawIp.packets > 0)
            r.ip = s.ip.report(d->cfg.topFlows, now);
        snap.streams.push_back(std::move(r));
    }
    if (d->pcap) {
        d->pcap->flush();
        snap.pcapOpen = d->pcap->isOpen();
        snap.pcapFile = d->pcap->fileName();
        snap.pcapPackets = d->pcap->packets();
        snap.pcapBytes = d->pcap->bytes();
    }
    snap.pcapError = d->pcapError;
    if (d->tun) {
        snap.tunOpen = d->tun->isOpen();
        snap.tunName = d->tun->name();
        snap.tunPackets = d->tun->packets();
        snap.tunErrors = d->tun->errors();
    }
    snap.tunError = d->tunError;
    snap.tunWarning = d->tunWarning;
    return snap;
}

} // namespace bb
} // namespace tbs
