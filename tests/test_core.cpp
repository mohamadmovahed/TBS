#include "StreamBuilders.h"

#include "core/BbFrameExtractor.h"
#include "core/BbFrameToTs.h"
#include "core/BbHeader.h"
#include "core/PlsUtil.h"
#include "core/TsAnalyzer.h"
#include "core/TunerConfig.h"

#include <cstdio>
#include <cstring>

using builders::Bytes;

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
    TunerConfig c;
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

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
