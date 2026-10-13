#include "StreamBuilders.h"

#include "tbsdvb/BbFrameExtractor.h"
#include "tbsdvb/BbFrameToTs.h"
#include "tbsdvb/BbHeader.h"
#include "tbsdvb/PlsUtil.h"
#include "tbsdvb/Monitor.h"
#include "tbsdvb/Psi.h"
#include "tbsdvb/Status.h"
#include "tbsdvb/TsAnalyzer.h"
#include "tbsdvb/TsFilter.h"
#include "tbsdvb/Types.h"

#include "tbsdvb/Receiver.h"
#include "tbsdvb/Sink.h"

#include <chrono>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using builders::Bytes;
using namespace tbs;

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const auto va = (a);                                                                              \
        const auto vb = (b);                                                                              \
        if (!(va == vb)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, \
                         static_cast<long long>(va), #b, static_cast<long long>(vb));                     \
            ++g_failures;                                                                                 \
        }                                                                                                 \
    } while (0)

static void testCrc8()
{
    const char *check = "123456789";
    // CRC-8/DVB-S2 check value
    CHECK_EQ(BbHeader::crc8(reinterpret_cast<const uint8_t *>(check), 9), 0xBC);

    Bytes df(100, 0x55);
    Bytes nm = builders::makeBbFrame(0x52, 0x05, 0, df, 0xFFFF, 0, false);
    BbHeader h;
    CHECK(h.parse(nm.data()));
    CHECK(h.mode == BbHeader::Mode::Normal);
    CHECK(h.streamType() == BbHeader::StreamType::GenericContinuous);
    CHECK(!h.singleInputStream());
    CHECK_EQ(h.isi(), 5);
    CHECK_EQ(h.dfl, 800);

    Bytes hem = builders::makeBbFrame(0xB0, 0, 0, df, 0, 0, true);
    CHECK(h.parse(hem.data()));
    CHECK(h.mode == BbHeader::Mode::HighEfficiency);

    hem[3] ^= 0x10;
    CHECK(!h.parse(hem.data()));
}

static void testPls()
{
    CHECK_EQ(PlsUtil::goldToRoot(0), 1u);
    CHECK_EQ(PlsUtil::rootToGold(1), 0u);
    for (uint32_t g : {1u, 8u, 131070u, 262141u, 12345u})
        CHECK_EQ(PlsUtil::rootToGold(PlsUtil::goldToRoot(g)), g);
}

static void testTunerConfig()
{
    TuningParams c;
    c.lnbType = LnbType::Universal;
    c.frequencyMHz = 11494;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1744000u);
    CHECK(!c.toneOn());
    c.frequencyMHz = 12000;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1400000u);
    CHECK(c.toneOn());

    c.lnbType = LnbType::SingleLo;
    c.lnbLo1MHz = 5150;
    c.frequencyMHz = 3800;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1350000u);
    CHECK(!c.toneOn());

    c.lnbType = LnbType::Direct;
    c.frequencyMHz = 1210.5;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1210500u);

    c.inputStreamId = -1;
    CHECK_EQ(c.dvbStreamId(), ~0u);
    c.inputStreamId = 3;
    c.plsMode = PlsMode::Gold;
    c.plsCode = 8;
    CHECK_EQ(c.dvbStreamId(), (3u | (8u << 8) | (1u << 26)));
    c.plsMode = PlsMode::Root;
    c.plsCode = PlsUtil::goldToRoot(77);
    CHECK_EQ(c.goldCode(), 77u);
}

static void testTsAnalyzer()
{
    Bytes stream = {0x12, 0x47, 0x00, 0x99}; // garbage incl. a false sync byte
    for (int i = 0; i < 10; ++i) {
        Bytes p = builders::makeTsPacket(0x100, static_cast<uint8_t>(i), 0xAA);
        stream.insert(stream.end(), p.begin(), p.end());
    }
    TsAnalyzer ts;
    size_t got = 0;
    auto cb = [&](const uint8_t *pk, size_t n) {
        for (size_t i = 0; i < n; ++i)
            CHECK_EQ(pk[i * 188], 0x47);
        got += n;
    };
    // Feed in awkward chunk sizes
    for (size_t off = 0; off < stream.size(); off += 77)
        ts.push(stream.data() + off, std::min<size_t>(77, stream.size() - off), cb);
    CHECK_EQ(got, 10u);
    CHECK_EQ(ts.ccErrors(), 0u);
    CHECK_EQ(ts.pidCount(0x100), 10u);

    Bytes jump = builders::makeTsPacket(0x100, 14, 0xAA);
    Bytes after = builders::makeTsPacket(0x100, 15, 0xAA);
    ts.push(jump.data(), jump.size(), cb);
    ts.push(after.data(), after.size(), cb);
    CHECK_EQ(ts.ccErrors(), 1u);
    CHECK_EQ(got, 12u);
}

static std::vector<Bytes> makeProgramPackets(size_t n)
{
    std::vector<Bytes> pk;
    for (size_t i = 0; i < n; ++i) {
        Bytes p = builders::makeTsPacket(static_cast<uint16_t>(0x100 + (i % 3)), static_cast<uint8_t>(i / 3),
                                         static_cast<uint8_t>(i));
        for (size_t j = 4; j < 188; ++j)
            p[j] = static_cast<uint8_t>(i * 31 + j);
        pk.push_back(p);
    }
    return pk;
}

static void testRoundTrip(bool hem, size_t dfl)
{
    const auto packets = makeProgramPackets(600);
    const auto frames = builders::tsToBbFrames(packets, dfl, hem);

    Bytes pseudo;
    uint8_t cc = 0;
    for (const Bytes &f : frames)
        for (const Bytes &p : builders::bbFrameToPseudoTs(f, cc, 0x0118))
            pseudo.insert(pseudo.end(), p.begin(), p.end());

    TsAnalyzer ts;
    BbFrameExtractor bbx;
    BbFrameToTs b2t;
    std::vector<Bytes> recovered;
    size_t frameIdx = 0;
    bool framesMatch = true;

    auto onFrame = [&](const uint8_t *f, size_t len, const BbHeader &h) {
        if (frameIdx >= frames.size() || frames[frameIdx].size() != len
            || std::memcmp(frames[frameIdx].data(), f, len) != 0)
            framesMatch = false;
        ++frameIdx;
        b2t.pushFrame(f, len, h, [&](const uint8_t *p) { recovered.emplace_back(p, p + 188); });
    };
    for (size_t off = 0; off < pseudo.size(); off += 1000) {
        ts.push(pseudo.data() + off, std::min<size_t>(1000, pseudo.size() - off), [&](const uint8_t *pk, size_t n) {
            for (size_t i = 0; i < n; ++i)
                bbx.pushPacket(pk + i * 188, onFrame);
        });
    }

    CHECK(framesMatch);
    CHECK_EQ(bbx.frames(), frames.size());
    CHECK_EQ(bbx.crcErrors(), 0u);
    CHECK_EQ(bbx.sequenceErrors(), 0u);
    CHECK_EQ(bbx.lengthErrors(), 0u);
    CHECK_EQ(ts.ccErrors(), 0u);
    // The first frame starts with a packet boundary, so every packet but a
    // possible trailing partial one must come back unchanged.
    CHECK(recovered.size() + 1 >= packets.size());
    bool same = true;
    for (size_t i = 0; i < recovered.size(); ++i)
        same = same && recovered[i] == packets[i];
    CHECK(same);
}

static void testExtractorErrors()
{
    Bytes df(3000);
    for (size_t i = 0; i < df.size(); ++i)
        df[i] = static_cast<uint8_t>(i);
    const Bytes frame = builders::makeBbFrame(0x70, 0, 0, df, 0, 0);
    uint8_t cc = 0;
    auto pk1 = builders::bbFrameToPseudoTs(frame, cc);
    auto pk2 = builders::bbFrameToPseudoTs(frame, cc);
    auto pk3 = builders::bbFrameToPseudoTs(frame, cc);
    pk2.erase(pk2.begin() + 5); // lose a packet in the second frame

    BbFrameExtractor bbx;
    size_t ok = 0;
    auto cb = [&](const uint8_t *, size_t len, const BbHeader &) {
        CHECK_EQ(len, frame.size());
        ++ok;
    };
    for (auto *v : {&pk1, &pk2, &pk3})
        for (const Bytes &p : *v)
            bbx.pushPacket(p.data(), cb);
    CHECK_EQ(ok, 2u);
    CHECK_EQ(bbx.sequenceErrors(), 1u);

    // Frame that ends exactly on a packet boundary (no short last packet)
    BbFrameExtractor bbx2;
    Bytes df2(179 * 3 - 10);
    const Bytes exact = builders::makeBbFrame(0x70, 0, 0, df2, 0, 0);
    cc = 0;
    size_t n2 = 0;
    for (int rep = 0; rep < 3; ++rep)
        for (const Bytes &p : builders::bbFrameToPseudoTs(exact, cc))
            bbx2.pushPacket(p.data(), [&](const uint8_t *, size_t, const BbHeader &) { ++n2; });
    CHECK_EQ(n2, 3u);
}

// Library end to end: replay a pseudo-TS file through tbs::Receiver in BBFrame mode
// and check frames, recovered TS, the file sink and the stats.
static void testReceiverReplay()
{
    const auto packets = makeProgramPackets(3000);
    const auto frames = builders::tsToBbFrames(packets, 5000);
    const std::string in = "tbs_test_input.ts";
    const std::string out = "tbs_test_output.bbf";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        uint8_t cc = 0;
        for (const Bytes &fr : frames)
            for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc))
                std::fwrite(p.data(), 1, 188, f);
        std::fclose(f);
    }

    ReceiverConfig cfg;
    cfg.device.kind = DeviceKind::FileReplay;
    cfg.device.replayFile = in;
    cfg.device.replayLoop = false;
    cfg.capture = CaptureMode::BBFrame;
    cfg.file.enabled = true;
    cfg.file.path = out;
    cfg.file.timestampSuffix = false;
    cfg.file.bbFormat = BbFileFormat::Raw;

    Receiver rx;
    size_t gotFrames = 0;
    Bytes previewTs;
    std::string error = "none";
    Receiver::Callbacks cb;
    cb.onBbFrame = [&](const uint8_t *, size_t, const BbHeader &) { ++gotFrames; };
    cb.onPreviewTs = [&](const uint8_t *p, size_t n) { previewTs.insert(previewTs.end(), p, p + n); };
    cb.onStateChanged = [&](ReceiverState s, const std::string &e) {
        if (s == ReceiverState::Stopped)
            error = e;
    };
    rx.setCallbacks(cb);
    rx.setPreviewEnabled(true);
    rx.setStatusInterval(50);
    CHECK(rx.start(cfg));
    for (int i = 0; i < 200 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rx.stop();

    CHECK(error.empty());
    CHECK_EQ(gotFrames, frames.size());
    CHECK_EQ(previewTs.size(), packets.size() * 188);
    bool same = previewTs.size() == packets.size() * 188;
    for (size_t i = 0; same && i < packets.size(); ++i)
        same = std::memcmp(previewTs.data() + i * 188, packets[i].data(), 188) == 0;
    CHECK(same);

    size_t expectedBytes = 0;
    for (const Bytes &fr : frames)
        expectedBytes += fr.size();
    std::FILE *f = std::fopen(out.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        std::fseek(f, 0, SEEK_END);
        CHECK_EQ(static_cast<size_t>(std::ftell(f)), expectedBytes);
        std::fclose(f);
    }
    std::remove(in.c_str());
    std::remove(out.c_str());

    // Opening a missing file reports an error through the state callback
    cfg.device.replayFile = "does_not_exist.ts";
    cfg.file.enabled = false;
    error.clear();
    CHECK(rx.start(cfg));
    for (int i = 0; i < 100 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rx.stop();
    CHECK(!error.empty());
    CHECK(!rx.lastError().empty());
}

// ---------------------------------------------------------------------------
// Network output
// ---------------------------------------------------------------------------

static void testNetworkUrl()
{
    TsNetworkOutput o;
    CHECK(parseNetworkUrl("udp://239.1.1.1:1234", &o));
    CHECK(o.protocol == NetProtocol::Udp);
    CHECK(o.host == "239.1.1.1");
    CHECK_EQ(o.port, 1234);
    CHECK_EQ(o.programNumber, 0);
    CHECK(networkUrl(o) == "udp://239.1.1.1:1234");

    CHECK(parseNetworkUrl("rtp://[ff15::7]:5004?program=101&pids=0x12,256&ttl=4&iface=eth1&isi=3&pkts=5&null=1", &o));
    CHECK(o.protocol == NetProtocol::Rtp);
    CHECK(o.host == "ff15::7");
    CHECK_EQ(o.port, 5004);
    CHECK_EQ(o.programNumber, 101);
    CHECK_EQ(o.pids.size(), 2u);
    CHECK_EQ(o.pids[0], 0x12);
    CHECK_EQ(o.pids[1], 256);
    CHECK_EQ(o.ttl, 4);
    CHECK(o.interfaceName == "eth1");
    CHECK_EQ(o.isi, 3);
    CHECK_EQ(o.packetsPerDatagram, 5);
    CHECK(!o.dropNullPackets);
    TsNetworkOutput back;
    CHECK(parseNetworkUrl(networkUrl(o), &back));
    CHECK(networkUrl(back) == networkUrl(o));

    CHECK(parseNetworkUrl("srt://:9000?mode=listener&latency=300&passphrase=my%20secret%21", &o));
    CHECK(o.protocol == NetProtocol::Srt);
    CHECK(o.srtMode == SrtMode::Listener);
    CHECK(o.host.empty());
    CHECK_EQ(o.srtLatencyMs, 300);
    CHECK(o.srtPassphrase == "my secret!");
    CHECK(parseNetworkUrl(networkUrl(o), &back));
    CHECK(back.srtPassphrase == "my secret!");
    CHECK(back.srtMode == SrtMode::Listener);
    CHECK(networkUrl(o, true).find("passphrase=***") != std::string::npos);
    CHECK(networkUrl(o, true).find("secret") == std::string::npos);

    std::string err;
    CHECK(!parseNetworkUrl("http://x:1", &o, &err));
    CHECK(!err.empty());
    CHECK(!parseNetworkUrl("udp://239.1.1.1", &o));
    CHECK(!parseNetworkUrl("udp://:1234", &o));          // no destination
    CHECK(!parseNetworkUrl("udp://1.2.3.4:99999", &o));
    CHECK(!parseNetworkUrl("udp://1.2.3.4:5?pids=0x2000", &o));
    CHECK(!parseNetworkUrl("udp://1.2.3.4:5?pkts=8", &o));
    CHECK(!parseNetworkUrl("udp://1.2.3.4:5?foo=1", &o));
    CHECK(!parseNetworkUrl("srt://h:5?passphrase=short", &o));
}

// MPTS: program 1 (PMT 0x100: video 0x101 = PCR, audio 0x102 with ECM 0x103),
// program 2 (PMT 0x200: video 0x201), SDT, nulls. pmt1Extra adds ES 0x104 to program 1.
static std::vector<Bytes> makeMpts(size_t rounds, bool pmt1Extra = false, uint16_t pmt1Pid = 0x100,
                                   uint8_t patVersion = 0)
{
    std::map<uint16_t, uint8_t> cc;
    auto pat = [&] {
        Bytes body;
        builders::put16(body, 1);
        builders::put16(body, static_cast<uint16_t>(0xE000 | pmt1Pid));
        builders::put16(body, 2);
        builders::put16(body, 0xE200);
        return builders::makeSection(0x00, 0x0042, patVersion, body);
    }();
    auto pmt = [](uint16_t program, uint16_t pcr, std::vector<std::pair<uint8_t, uint16_t>> es, uint16_t ecmOn,
                  uint8_t version) {
        Bytes body;
        builders::put16(body, static_cast<uint16_t>(0xE000 | pcr));
        builders::put16(body, 0xF000);
        for (auto &e : es) {
            body.push_back(e.first);
            builders::put16(body, static_cast<uint16_t>(0xE000 | e.second));
            Bytes info;
            if (e.second == ecmOn)
                builders::addDescriptor(info, 0x09, {0x0B, 0x00, 0xE1, 0x03}); // CA 0x0B00, ECM PID 0x103
            builders::put16(body, static_cast<uint16_t>(0xF000 | info.size()));
            body.insert(body.end(), info.begin(), info.end());
        }
        return builders::makeSection(0x02, program, version, body);
    };
    std::vector<std::pair<uint8_t, uint16_t>> es1 = {{0x1B, 0x101}, {0x0F, 0x102}};
    if (pmt1Extra)
        es1.push_back({0x06, 0x104});
    const Bytes pmt1 = pmt(1, 0x101, es1, 0x102, pmt1Extra ? 1 : 0);
    const Bytes pmt2 = pmt(2, 0x201, {{0x02, 0x201}}, 0, 0);

    std::vector<Bytes> out;
    auto section = [&](const Bytes &sec, uint16_t pid) {
        for (Bytes &p : builders::sectionToTs(sec, pid, cc[pid]))
            out.push_back(p);
    };
    auto data = [&](uint16_t pid) { out.push_back(builders::makeTsPacket(pid, cc[pid]++, static_cast<uint8_t>(pid))); };
    for (size_t r = 0; r < rounds; ++r) {
        section(pat, 0);
        section(pmt1, pmt1Pid);
        section(pmt2, 0x200);
        section(builders::makeSection(0x42, 0x42, 0, {0x00, 0x01, 0xFF}), 0x11);
        for (int i = 0; i < 4; ++i) {
            data(0x101);
            data(0x102);
            data(0x201);
            data(0x1FFF);
        }
        data(0x103);
        if (pmt1Extra)
            data(0x104);
    }
    return out;
}

static std::map<uint16_t, size_t> pidHistogram(const std::vector<Bytes> &pk)
{
    std::map<uint16_t, size_t> h;
    for (const Bytes &p : pk)
        ++h[static_cast<uint16_t>(((p[1] & 0x1F) << 8) | p[2])];
    return h;
}

static void testTsFilter()
{
    const auto in = makeMpts(10);
    const auto inHist = pidHistogram(in);

    // Whole stream: only the null packets go
    {
        TsFilter f;
        std::vector<Bytes> out;
        for (const Bytes &p : in)
            f.push(p.data(), [&](const uint8_t *q) { out.emplace_back(q, q + 188); });
        auto h = pidHistogram(out);
        CHECK_EQ(h.count(0x1FFF), 0u);
        CHECK_EQ(out.size(), in.size() - inHist.at(0x1FFF));
    }
    // PID list
    {
        TsFilterOptions o;
        o.pids = {0x201, 0x11};
        TsFilter f(o);
        std::vector<Bytes> out;
        for (const Bytes &p : in)
            f.push(p.data(), [&](const uint8_t *q) { out.emplace_back(q, q + 188); });
        auto h = pidHistogram(out);
        CHECK_EQ(h.size(), 2u);
        CHECK_EQ(h[0x201], inHist.at(0x201));
        CHECK_EQ(h[0x11], inHist.at(0x11));
    }
    // Service 1: rewritten PAT, PMT, ES, PCR, ECM; nothing of service 2
    {
        TsFilterOptions o;
        o.programNumber = 1;
        TsFilter f(o);
        std::vector<Bytes> out;
        for (const Bytes &p : in)
            f.push(p.data(), [&](const uint8_t *q) { out.emplace_back(q, q + 188); });
        CHECK(f.programFound());
        CHECK(f.pmtSeen());
        CHECK_EQ(f.pmtPid(), 0x100);
        const std::vector<uint16_t> expect = {0x100, 0x101, 0x102, 0x103};
        CHECK(f.servicePids() == expect);
        auto h = pidHistogram(out);
        CHECK_EQ(h.count(0x200), 0u);
        CHECK_EQ(h.count(0x201), 0u);
        CHECK_EQ(h.count(0x11), 0u);
        CHECK_EQ(h.count(0x1FFF), 0u);
        CHECK_EQ(h[0], 10u);
        CHECK_EQ(h[0x100], 10u);
        // ES packets before the first PMT are dropped, all later ones pass
        CHECK_EQ(h[0x101], inHist.at(0x101));
        CHECK_EQ(h[0x102], inHist.at(0x102));
        CHECK_EQ(h[0x103], inHist.at(0x103));
        // The PAT lists only program 1, has a valid CRC and continuous CC
        int lastCc = -1;
        bool patOk = true;
        for (const Bytes &p : out) {
            if (p[1] != 0x40 || p[2] != 0x00)
                continue;
            const uint8_t *s = p.data() + 5;
            const size_t len = 3 + (((s[1] & 0x0F) << 8) | s[2]);
            patOk = patOk && len == 16 && crc32Mpeg(s, len) == 0 && ((s[3] << 8) | s[4]) == 0x42
                    && ((s[8] << 8) | s[9]) == 1 && (((s[10] & 0x1F) << 8) | s[11]) == 0x100;
            if (lastCc >= 0)
                patOk = patOk && (p[3] & 0x0F) == ((lastCc + 1) & 0x0F);
            lastCc = p[3] & 0x0F;
        }
        CHECK(patOk);

        // PMT update adds ES 0x104; a PAT update moves the PMT to 0x300 (new PAT version)
        std::vector<Bytes> out2;
        for (const Bytes &p : makeMpts(3, true))
            f.push(p.data(), [&](const uint8_t *q) { out2.emplace_back(q, q + 188); });
        CHECK(pidHistogram(out2)[0x104] > 0);
        const uint8_t oldVersion = static_cast<uint8_t>((out[0][5 + 5] >> 1) & 0x1F);
        std::vector<Bytes> out3;
        for (const Bytes &p : makeMpts(3, false, 0x300, 1))
            f.push(p.data(), [&](const uint8_t *q) { out3.emplace_back(q, q + 188); });
        CHECK_EQ(f.pmtPid(), 0x300);
        auto h3 = pidHistogram(out3);
        CHECK(h3[0x300] > 0);
        CHECK_EQ(h3.count(0x100), 0u);
        uint8_t newVersion = oldVersion;
        for (const Bytes &p : out3)
            if (p[1] == 0x40 && p[2] == 0)
                newVersion = static_cast<uint8_t>((p[5 + 5] >> 1) & 0x1F);
        CHECK(newVersion != oldVersion);
    }
    // Unknown service: nothing at all
    {
        TsFilterOptions o;
        o.programNumber = 77;
        TsFilter f(o);
        size_t n = 0;
        for (const Bytes &p : in)
            f.push(p.data(), [&](const uint8_t *) { ++n; });
        CHECK_EQ(n, 0u);
        CHECK(!f.programFound());
    }
}

// Minimal UDP receiver on 127.0.0.1 with an ephemeral port
struct TestUdpReceiver
{
#ifdef _WIN32
    SOCKET fd = INVALID_SOCKET;
#else
    int fd = -1;
#endif
    uint16_t port = 0;

    TestUdpReceiver()
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        int rcv = 8 * 1024 * 1024;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&rcv), sizeof(rcv));
        sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
#ifdef _WIN32
        int len = sizeof(a);
        DWORD tv = 300;
#else
        socklen_t len = sizeof(a);
        timeval tv{0, 300000};
#endif
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
        port = ntohs(a.sin_port);
    }
    ~TestUdpReceiver()
    {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
    }
    // Returns an empty buffer on timeout
    Bytes receive()
    {
        Bytes b(65536);
        const auto n = ::recv(fd, reinterpret_cast<char *>(b.data()), static_cast<int>(b.size()), 0);
        b.resize(n > 0 ? static_cast<size_t>(n) : 0);
        return b;
    }
};

static void testUdpRtpOutput()
{
    const auto in = makeMpts(10);
    for (int proto = 0; proto < 2; ++proto) {
        TestUdpReceiver rx;
        CHECK(rx.port != 0);
        TsNetworkOutput o;
        o.protocol = proto ? NetProtocol::Rtp : NetProtocol::Udp;
        o.host = "127.0.0.1";
        o.port = rx.port;
        o.programNumber = 1;
        TsNetworkSink sink(o, CaptureMode::TransportStream);
        std::string err;
        CHECK(sink.open(&err));
        CHECK(sink.status().state.find("not in the PAT") != std::string::npos);
        Bytes all;
        for (const Bytes &p : in)
            all.insert(all.end(), p.begin(), p.end());
        sink.writeTs(all.data(), in.size());
        sink.close(); // flushes the last partial datagram

        TsFilterOptions fo;
        fo.programNumber = 1;
        TsFilter ref(fo);
        size_t expected = 0;
        for (const Bytes &p : in)
            ref.push(p.data(), [&](const uint8_t *) { ++expected; });

        size_t got = 0;
        size_t datagrams = 0;
        int lastSeq = -1;
        bool headersOk = true;
        while (true) {
            Bytes d = rx.receive();
            if (d.empty())
                break;
            ++datagrams;
            size_t off = 0;
            if (proto) {
                headersOk = headersOk && d.size() >= 12 && d[0] == 0x80 && (d[1] & 0x7F) == 33;
                const int seq = (d[2] << 8) | d[3];
                if (lastSeq >= 0)
                    headersOk = headersOk && seq == ((lastSeq + 1) & 0xFFFF);
                lastSeq = seq;
                off = 12;
            }
            headersOk = headersOk && (d.size() - off) % 188 == 0 && (d.size() - off) <= 7 * 188;
            for (size_t i = off; i + 188 <= d.size(); i += 188) {
                headersOk = headersOk && d[i] == 0x47;
                ++got;
            }
        }
        CHECK(headersOk);
        CHECK_EQ(got, expected);
        CHECK_EQ(datagrams, (expected + 6) / 7);
        const NetOutputStatus st = sink.status();
        CHECK_EQ(st.packets, expected);
        CHECK_EQ(st.sendErrors, 0u);
        CHECK(st.state == "sending");
    }

    // Bad address / unknown interface fail at open
    TsNetworkOutput bad;
    bad.host = "no-such-host.invalid";
    std::string err;
    CHECK(!TsNetworkSink(bad, CaptureMode::TransportStream).open(&err));
    CHECK(!err.empty());
#ifdef __linux__
    bad.host = "239.1.1.1";
    bad.interfaceName = "nosuchif0";
    CHECK(!TsNetworkSink(bad, CaptureMode::TransportStream).open(&err));
#endif
    if (!TsNetworkSink::srtAvailable()) {
        TsNetworkOutput srt;
        srt.protocol = NetProtocol::Srt;
        CHECK(!TsNetworkSink(srt, CaptureMode::TransportStream).open(&err));
    }
}

// BBFrame capture -> Receiver -> TS rebuilt from ISI 2 of a multistream -> UDP
static void testReceiverNetworkOutput()
{
    const auto ts = makeMpts(40);
    // ISI 1 carries other data, ISI 2 our MPTS
    const auto framesIsi2 = builders::tsToBbFrames(ts, 3000);
    const auto otherTs = makeProgramPackets(2000);
    const auto framesIsi1 = builders::tsToBbFrames(otherTs, 3000);
    const std::string in = "tbs_test_net.ts";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        uint8_t cc = 0;
        auto put = [&](Bytes fr, uint8_t isi) {
            fr[0] = static_cast<uint8_t>(fr[0] & ~0x20); // MIS
            fr[1] = isi;
            fr[9] = BbHeader::crc8(fr.data(), 9);
            for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc))
                std::fwrite(p.data(), 1, 188, f);
        };
        for (size_t i = 0; i < std::max(framesIsi1.size(), framesIsi2.size()); ++i) {
            if (i < framesIsi1.size())
                put(framesIsi1[i], 1);
            if (i < framesIsi2.size())
                put(framesIsi2[i], 2);
        }
        std::fclose(f);
    }
    TestUdpReceiver rx;
    ReceiverConfig cfg;
    cfg.device.kind = DeviceKind::FileReplay;
    cfg.device.replayFile = in;
    cfg.device.replayLoop = false;
    cfg.device.replayRateMbps = 40;
    cfg.capture = CaptureMode::BBFrame;
    TsNetworkOutput o;
    o.host = "127.0.0.1";
    o.port = rx.port;
    o.isi = 2;
    o.programNumber = 2;
    cfg.network.push_back(o);
    o.enabled = false; // disabled outputs are skipped
    cfg.network.push_back(o);

    std::vector<Bytes> got;
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done) {
            Bytes d = rx.receive();
            for (size_t i = 0; i + 188 <= d.size(); i += 188)
                got.emplace_back(d.begin() + static_cast<std::ptrdiff_t>(i),
                                 d.begin() + static_cast<std::ptrdiff_t>(i + 188));
        }
    });
    Receiver r;
    std::vector<std::string> logs;
    std::mutex logMutex;
    StreamStats last;
    Receiver::Callbacks cb;
    cb.onLog = [&](const std::string &m) {
        std::lock_guard<std::mutex> lock(logMutex);
        logs.push_back(m);
    };
    cb.onStats = [&](const StreamStats &st) { last = st; };
    r.setCallbacks(cb);
    r.setStatusInterval(50);
    CHECK(r.start(cfg));
    for (int i = 0; i < 300 && r.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    r.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    done = true;
    reader.join();
    std::remove(in.c_str());

    TsFilterOptions fo;
    fo.programNumber = 2;
    TsFilter ref(fo);
    std::vector<Bytes> expected;
    for (const Bytes &p : ts)
        ref.push(p.data(), [&](const uint8_t *q) { expected.emplace_back(q, q + 188); });
    CHECK_EQ(got.size(), expected.size());
    CHECK(got == expected);
    CHECK_EQ(last.network.size(), 1u);
    if (!last.network.empty()) // the last partial datagram may be sent after the final report
        CHECK(last.network[0].packets <= expected.size() && last.network[0].packets + 7 > expected.size());
    bool logged = false;
    for (const std::string &m : logs)
        logged = logged || m.find("Streaming service 2 of ISI 2 to udp://127.0.0.1:") != std::string::npos;
    CHECK(logged);
}

// BBFrame mode with two TS input streams: the preview carries only the selected one
static void testPreviewStream()
{
    const auto tsA = makeProgramPackets(900);
    const auto tsB = makeMpts(20);
    const auto fa = builders::tsToBbFrames(tsA, 2000);
    const auto fb = builders::tsToBbFrames(tsB, 2000);
    const std::string in = "tbs_test_preview.ts";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        uint8_t cc = 0;
        auto put = [&](Bytes fr, uint8_t isi) {
            fr[0] = static_cast<uint8_t>(fr[0] & ~0x20);
            fr[1] = isi;
            fr[9] = BbHeader::crc8(fr.data(), 9);
            for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc))
                std::fwrite(p.data(), 1, 188, f);
        };
        for (size_t i = 0; i < std::max(fa.size(), fb.size()); ++i) {
            if (i < fa.size())
                put(fa[i], 1);
            if (i < fb.size())
                put(fb[i], 2);
        }
        std::fclose(f);
    }
    for (int want : {2, 1, -1}) {
        ReceiverConfig cfg;
        cfg.device.kind = DeviceKind::FileReplay;
        cfg.device.replayFile = in;
        cfg.device.replayLoop = false;
        cfg.capture = CaptureMode::BBFrame;
        Receiver rx;
        Bytes preview;
        Receiver::Callbacks cb;
        cb.onPreviewTs = [&](const uint8_t *p, size_t n) { preview.insert(preview.end(), p, p + n); };
        rx.setCallbacks(cb);
        rx.setPreviewEnabled(true);
        rx.setPreviewStream(want);
        CHECK(rx.start(cfg));
        for (int i = 0; i < 200 && rx.isRunning(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rx.stop();
        // -1 locks onto the first TS stream seen, ISI 1 here
        const auto &expect = want == 2 ? tsB : tsA;
        Bytes all;
        for (const Bytes &p : expect)
            all.insert(all.end(), p.begin(), p.end());
        CHECK_EQ(preview.size(), all.size());
        CHECK(preview == all);
    }
    std::remove(in.c_str());
}

static void testMonitor()
{
    MonitorConfig cfg;
    cfg.historySeconds = 30;
    cfg.noLockHoldSec = 3;
    cfg.lowCnr = true;
    cfg.cnrMinDb = 5.0;
    cfg.cnrHoldSec = 2;
    cfg.windowSec = 10;
    const std::string marker = "tbs_test_alarm.txt";
    std::remove(marker.c_str());
#ifndef _WIN32
    cfg.command = "echo {alarm} {state} {message} >> " + marker;
#endif
    Monitor m(cfg);
    std::vector<AlarmEvent> events;
    m.setAlarmCallback([&](const AlarmEvent &e) { events.push_back(e); });

    FrontendStatus fe;
    fe.valid = true;
    fe.flags = FrontendStatus::HasLock;
    fe.hasCnrDb = true;
    fe.cnrDb = 9.0;
    StreamStats st;
    st.inputMbps = 10.0;
    int64_t t = 1000000;
    auto step = [&](int n = 1) {
        for (int i = 0; i < n; ++i) {
            t += 1000;
            m.update(ReceiverState::Running, fe, st, t);
        }
    };
    step(5);
    CHECK_EQ(m.activeAlarms(), 0u);
    CHECK(events.empty());

    // Lock lost: raised after the hold time, cleared on relock
    fe.flags = FrontendStatus::HasSignal;
    step(2);
    CHECK_EQ(m.activeAlarms(), 0u);
    step(2);
    CHECK_EQ(m.activeAlarms(), 1u);
    CHECK(!events.empty() && events.back().type == AlarmType::NoLock && events.back().raised);
    fe.flags = FrontendStatus::HasLock;
    step();
    CHECK_EQ(m.activeAlarms(), 0u);
    CHECK(events.back().type == AlarmType::NoLock && !events.back().raised);

    // Low C/N
    fe.cnrDb = 3.5;
    step(3);
    CHECK(m.alarms()[static_cast<size_t>(AlarmType::LowCnr)].active);
    CHECK(m.alarms()[static_cast<size_t>(AlarmType::LowCnr)].message.find("3.5 dB") != std::string::npos);
    fe.cnrDb = 8.0;
    step();
    CHECK(!m.alarms()[static_cast<size_t>(AlarmType::LowCnr)].active);

    // CC errors in the window: raised at once, cleared when they leave the window
    st.ccErrors += 4;
    step();
    const Alarm cc = m.alarms()[static_cast<size_t>(AlarmType::CcErrors)];
    CHECK(cc.active);
    CHECK(cc.message.find("4 CC errors") != std::string::npos);
    step(9);
    CHECK(m.alarms()[static_cast<size_t>(AlarmType::CcErrors)].active);
    step(2);
    CHECK(!m.alarms()[static_cast<size_t>(AlarmType::CcErrors)].active);
    CHECK_EQ(m.alarms()[static_cast<size_t>(AlarmType::CcErrors)].raisedCount, 1u);

    // No data
    st.inputMbps = 0.0;
    step(6);
    CHECK(m.alarms()[static_cast<size_t>(AlarmType::NoData)].active);
    st.inputMbps = 10.0;
    step();

    // Output errors: stopped recording
    st.fileStopped = true;
    step();
    CHECK(m.alarms()[static_cast<size_t>(AlarmType::OutputError)].active);
    st.fileStopped = false;
    step();
    CHECK(!m.alarms()[static_cast<size_t>(AlarmType::OutputError)].active);

    // History is trimmed to historySeconds; samples carry the per-interval counters
    const auto h = m.history();
    CHECK(h.size() <= 31u && h.size() >= 29u);
    CHECK(h.back().timeMs == t);
    size_t ccTotal = 0;
    for (const auto &s2 : m.history(0))
        ccTotal += s2.ccErrors;
    CHECK(ccTotal == 4u || h.front().timeMs > t - 30000);

    // A counter that goes back (new capture) counts as fresh errors, not a huge delta
    st.ccErrors = 1;
    step();
    CHECK_EQ(m.history().back().ccErrors, 1u);

    // CSV
    const std::string csv = "tbs_test_monitor.csv";
    std::string err;
    CHECK(m.writeCsv(csv, t - 5000, &err));
    std::ifstream in(csv);
    std::string line;
    int lines = 0;
    std::getline(in, line);
    CHECK(line.rfind("time,unix_ms,running,locked", 0) == 0);
    while (std::getline(in, line))
        ++lines;
    CHECK_EQ(lines, 6);
    in.close();
    std::remove(csv.c_str());

    // Disabled alarms never fire; reset clears active ones
    cfg.noLock = false;
    m.setConfig(cfg);
    fe.flags = 0;
    step(5);
    CHECK(!m.alarms()[static_cast<size_t>(AlarmType::NoLock)].active);
    st.ccErrors += 10;
    step();
    CHECK(m.activeAlarms() > 0);
    m.reset();
    CHECK_EQ(m.activeAlarms(), 0u);
    CHECK(std::string(alarmTypeName(AlarmType::BbCrcErrors)) == "bbframe-crc");

#ifndef _WIN32
    // The command ran for each raise/clear (detached threads, any order), with shell quoting
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::ifstream mk(marker);
    const std::string all((std::istreambuf_iterator<char>(mk)), std::istreambuf_iterator<char>());
    CHECK(all.find("no-lock raised no lock for 3 s\n") != std::string::npos);
    CHECK(all.find("low-cnr cleared C/N back above the limit\n") != std::string::npos);
    mk.close();
    std::remove(marker.c_str());
#endif
}

#if defined(TBS_TEST_LINUX_INTERNALS)
#include "platform/LinuxDvbDevice.h"

// Linux diagnostics against fake /sys, /proc, /dev and /lib trees
static void testLinuxDiagnostics()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute("tbs_test_root");
    auto write = [&](const std::string &rel, const std::string &content) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << content;
    };
    auto has = [](const std::vector<DiagnosticItem> &items, DiagnosticItem::Level level, const std::string &text) {
        for (const auto &i : items)
            if (i.level == level && i.message.find(text) != std::string::npos)
                return true;
        return false;
    };
    DeviceSelection sel;

    // Card present, no driver bound, module installed but not loaded, no /dev/dvb
    fs::remove_all(root);
    write("proc/sys/kernel/osrelease", "6.1.0-test\n");
    write("proc/modules", "snd 1 0 - Live 0x0\n");
    const std::string pci = "sys/bus/pci/devices/0000:03:00.0/";
    write(pci + "vendor", "0x544d\n");
    write(pci + "device", "0x6178\n");
    write(pci + "subsystem_vendor", "0x6903\n");
    write(pci + "subsystem_device", "0x0021\n");
    write("lib/modules/6.1.0-test/updates/tbsecp3.ko.xz", "");
    auto items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "TBS 6903x found at PCI 0000:03:00.0 but no driver"));
    CHECK(has(items, DiagnosticItem::Level::Warning, "tbsecp3 is installed"));
    CHECK(has(items, DiagnosticItem::Level::Error, "/dev/dvb has no adapters"));

    // Module missing for the running kernel
    fs::remove(root / "lib/modules/6.1.0-test/updates/tbsecp3.ko.xz");
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "not installed for the running kernel 6.1.0-test"));

    // Everything in place; the user asks for a frontend that does not exist
    write("proc/modules", "stid135 1 0 - Live 0x0\ntbsecp3 1 0 - Live 0x0\ndvb_core 1 0 - Live 0x0\n");
    fs::create_directories(root / "sys/bus/pci/drivers/tbsecp3");
    fs::create_directory_symlink(root / "sys/bus/pci/drivers/tbsecp3", root / pci / "driver");
    write("dev/dvb/adapter0/frontend0", "");
    write("dev/dvb/adapter0/demux0", "");
    write("dev/dvb/adapter1/frontend0", "");
    write("sys/module/stid135/parameters/bbframe", "0\n");
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Ok, "TBS 6903x found at PCI 0000:03:00.0, driver: tbsecp3"));
    CHECK(has(items, DiagnosticItem::Level::Ok, "Module tbsecp3 loaded"));
    CHECK(has(items, DiagnosticItem::Level::Ok, "/dev/dvb/adapter0/frontend0 is accessible"));
    CHECK(has(items, DiagnosticItem::Level::Info, "stid135 bbframe=0"));
    sel.adapter = 2;
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "/dev/dvb/adapter2/frontend0 does not exist"));
    CHECK(!Devices::formatDiagnostics(items).empty());
    fs::remove_all(root);
}
#endif

// A sink that is too slow: the capture thread must not stall, data is dropped and counted
class SlowSink : public IOutputSink
{
public:
    bool open(std::string *) override { return true; }
    void close() override {}
    bool acceptsTs(CaptureMode) const override { return true; }
    bool acceptsBbFrames(CaptureMode) const override { return false; }
    void writeTs(const uint8_t *, size_t count) override
    {
        packets += count;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    void writeBbFrame(const uint8_t *, size_t, const BbHeader &) override {}
    size_t packets = 0;
};

static void testPipelineBackpressure()
{
    const std::string in = "tbs_test_pipe.ts";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        for (int i = 0; i < 60000; ++i) { // ~11 MB
            Bytes p = builders::makeTsPacket(0x100, static_cast<uint8_t>(i), 0x11);
            std::fwrite(p.data(), 1, 188, f);
        }
        std::fclose(f);
    }
    ReceiverConfig cfg;
    cfg.device.kind = DeviceKind::FileReplay;
    cfg.device.replayFile = in;
    cfg.device.replayLoop = false;

    Receiver rx;
    auto sink = std::make_shared<SlowSink>();
    rx.addSink(sink);
    rx.setPipelineBufferSize(1u << 20); // tiny buffer
    rx.setStatusInterval(50);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(rx.start(cfg));
    for (int i = 0; i < 500 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rx.stop();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const StreamStats st = rx.stats();
    CHECK_EQ(st.bytesIn, 60000u * 188);        // the reader consumed everything
    CHECK(st.pipelineDroppedBytes > 0);          // ... and dropped what processing could not take
    CHECK(st.pipelineCapacityBytes == (1u << 20));
    CHECK_EQ(st.tsPackets * 188 + st.pipelineDroppedBytes, st.bytesIn);
    CHECK_EQ(sink->packets, st.tsPackets);
    CHECK(secs < 5.0);
    std::remove(in.c_str());
}

static void testFileSegments()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::absolute("tbs_test_rec");
    fs::remove_all(dir);
    FileOutputOptions opt;
    opt.enabled = true;
    opt.path = (dir / "cap.ts").string();
    opt.timestampSuffix = false;
    opt.splitSizeMB = 1;
    opt.keepFiles = 2;
    opt.minFreeMB = 1;
    FileSink sink(opt, CaptureMode::TransportStream);
    std::string err;
    CHECK(sink.open(&err));
    std::vector<uint8_t> chunk(188 * 1000, 0x47); // 188000 bytes per write
    // A segment is closed once it holds >= 1 MiB: 6 writes (1,128,000 bytes) -> 30 writes = 5 segments
    for (int i = 0; i < 30; ++i)
        sink.writeTs(chunk.data(), 1000);
    sink.close();
    CHECK_EQ(sink.segments(), 5u);
    CHECK_EQ(sink.bytesWritten(), 30u * 188000);
    CHECK(!fs::exists(dir / "cap_001.ts")); // rolled out (keep 2)
    CHECK(!fs::exists(dir / "cap_003.ts"));
    CHECK(fs::exists(dir / "cap_004.ts"));
    CHECK(fs::exists(dir / "cap_005.ts"));
    CHECK_EQ(fs::file_size(dir / "cap_004.ts"), 6u * 188000);
    StreamStats st;
    sink.fillStats(st);
    CHECK(st.fileName.find("cap_005.ts") != std::string::npos);
    CHECK(st.fileFreeBytes > 0);
    CHECK(!st.fileStopped);

    // Not enough free space: refuse to start
    FileOutputOptions full = opt;
    full.path = (dir / "full.ts").string();
    full.minFreeMB = 1ull << 40; // 1 EiB
    FileSink fullSink(full, CaptureMode::TransportStream);
    CHECK(!fullSink.open(&err));
    CHECK(err.find("free disk space") != std::string::npos);
    fs::remove_all(dir);
}

int main()
{
    testCrc8();
    testPls();
    testTunerConfig();
    testTsAnalyzer();
    testRoundTrip(false, 7264);
    testRoundTrip(false, 1000);
    testRoundTrip(true, 7264);
    testRoundTrip(true, 333);
    testExtractorErrors();
    testReceiverReplay();
    testPipelineBackpressure();
    testFileSegments();
    testNetworkUrl();
    testTsFilter();
    testUdpRtpOutput();
    testReceiverNetworkOutput();
    testPreviewStream();
    testMonitor();
#if defined(TBS_TEST_LINUX_INTERNALS)
    testLinuxDiagnostics();
#endif

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
