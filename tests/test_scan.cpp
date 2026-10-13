// Spectrum, blind scan and constellation through the Linux DVB backend against the
// simulated neumoDVB frontend in fake_frontend.cpp (run with LD_PRELOAD, see CMakeLists.txt).

#include "tbsdvb/Receiver.h"
#include "tbsdvb/Scan.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace tbs;

static int g_failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

static DeviceSelection fakeDevice()
{
    DeviceSelection sel;
    sel.kind = DeviceKind::LinuxDvb;
    sel.adapter = 97;
    sel.frontend = 0;
    sel.demux = 0;
    return sel;
}

static bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

static void testCapabilities()
{
    const ScanCapabilities caps = Scan::capabilities(fakeDevice());
    CHECK(caps.probed);
    CHECK(caps.neumoApi);
    CHECK(caps.spectrumFft && caps.spectrumSweep && caps.blindScan && caps.constellation);

    DeviceSelection missing = fakeDevice();
    missing.adapter = 96;
    CHECK(!Scan::capabilities(missing).probed);
}

static void testSpectrum()
{
    SpectrumRequest req;
    req.startMHz = 10700;
    req.endMHz = 12750;
    req.lnb.polarization = Polarization::HorizontalLeft18V;
    req.resolutionKHz = 500;
    SpectrumResult res;
    std::string err;
    int progressCalls = 0;
    const bool ok = Scan::spectrum(fakeDevice(), req, &res, &err, [&](double f, const std::string &) {
        if (f >= 0)
            ++progressCalls;
        return true;
    });
    if (!ok)
        std::fprintf(stderr, "spectrum: %s\n", err.c_str());
    CHECK(ok);
    CHECK(progressCalls >= 3); // two segments + done
    CHECK(res.decibel);
    CHECK(res.points.size() > 3000);
    CHECK(near(res.points.front().frequencyMHz, 10700, 0.6));
    CHECK(near(res.points.back().frequencyMHz, 12750, 0.6));
    bool sorted = true;
    for (size_t i = 1; i < res.points.size(); ++i)
        sorted = sorted && res.points[i].frequencyMHz >= res.points[i - 1].frequencyMHz;
    CHECK(sorted);

    // Horizontal carriers of the simulated sky, from the driver's candidate list
    const double expected[] = {10744, 11053, 11494, 11817, 12603};
    CHECK(res.peaks.size() == 5);
    for (size_t i = 0; i < res.peaks.size() && i < 5; ++i)
        CHECK(near(res.peaks[i].frequencyMHz, expected[i], 0.5));

    // findPeaks on the measured spectrum finds the same carriers
    const std::vector<SpectrumPeak> found = Scan::findPeaks(res.points);
    CHECK(found.size() == 5);
    for (size_t i = 0; i < found.size() && i < 5; ++i) {
        CHECK(near(found[i].frequencyMHz, expected[i], 1.5));
        CHECK(found[i].symbolRateKsps > 15000 && found[i].symbolRateKsps < 35000);
    }
    // Level at a carrier is well above the noise floor
    auto levelAt = [&](double f) {
        double best = -1e9, dist = 1e9;
        for (const auto &p : res.points)
            if (std::fabs(p.frequencyMHz - f) < dist) {
                dist = std::fabs(p.frequencyMHz - f);
                best = p.level;
            }
        return best;
    };
    CHECK(levelAt(11053) - levelAt(11300) > 10.0);
}

static void testBlindScan()
{
    BlindScanRequest req;
    req.startMHz = 10700;
    req.endMHz = 12750;
    req.lnb.polarization = Polarization::VerticalRight13V;
    std::vector<BlindScanCarrier> carriers;
    int live = 0;
    SpectrumResult spec;
    std::string err;
    const bool ok = Scan::blindScan(fakeDevice(), req, &carriers, &err, {}, [&](const BlindScanCarrier &) { ++live; }, &spec);
    if (!ok)
        std::fprintf(stderr, "blind scan: %s\n", err.c_str());
    CHECK(ok);
    CHECK(carriers.size() == 3);
    CHECK(live == 3);
    CHECK(!spec.points.empty());
    if (carriers.size() == 3) {
        CHECK(near(carriers[0].frequencyMHz, 10832.12, 0.01));
        CHECK(near(carriers[0].symbolRateKsps, 22000, 0.1));
        CHECK(carriers[0].deliverySystem == DeliverySystem::DvbS2);
        CHECK(carriers[1].deliverySystem == DeliverySystem::DvbS);
        CHECK(near(carriers[2].frequencyMHz, 12187.12, 0.01));
        CHECK(carriers[2].tuning.polarization == Polarization::VerticalRight13V);
        CHECK(carriers[2].tuning.toneOn()); // high band
        CHECK(carriers[0].hasCnrDb);
        CHECK(!carriers[0].describe().empty());
    }

    // Multistream carrier on H high band: ISI list read from the driver
    req.startMHz = 12500;
    req.lnb.polarization = Polarization::HorizontalLeft18V;
    CHECK(Scan::blindScan(fakeDevice(), req, &carriers, &err));
    CHECK(carriers.size() == 1);
    if (carriers.size() == 1) {
        CHECK((carriers[0].isis == std::vector<int>{1, 2, 3}));
        CHECK(carriers[0].tuning.inputStreamId == 1);
        CHECK(carriers[0].describe().find("3 streams") != std::string::npos);
    }

    // Cancelling stops the scan
    req.startMHz = 10700;
    CHECK(!Scan::blindScan(fakeDevice(), req, &carriers, &err, [](double, const std::string &) { return false; }));
    CHECK(err == "Cancelled");
}

static void testStockDriver()
{
    setenv("FAKE_DVB_NEUMO", "0", 1);
    const ScanCapabilities caps = Scan::capabilities(fakeDevice());
    CHECK(caps.probed && !caps.neumoApi);
    CHECK(caps.note.find("neumoDVB") != std::string::npos);
    SpectrumRequest req;
    SpectrumResult res;
    std::string err;
    CHECK(!Scan::spectrum(fakeDevice(), req, &res, &err));
    CHECK(err.find("not supported") != std::string::npos);
    unsetenv("FAKE_DVB_NEUMO");
}

static void testConstellationAndTuning(const std::string &tsFile)
{
    setenv("FAKE_DVB_TS", tsFile.c_str(), 1);
    Receiver rx;
    rx.setStatusInterval(50);
    rx.setConstellationSamples(1024);
    ReceiverConfig cfg;
    cfg.device = fakeDevice();
    cfg.tuning.frequencyMHz = 11494;
    cfg.tuning.symbolRateKsps = 22000;
    cfg.tuning.polarization = Polarization::HorizontalLeft18V;
    CHECK(rx.start(cfg));
    bool locked = false;
    std::vector<ConstellationSample> iq;
    bool supported = false;
    for (int i = 0; i < 100 && (iq.empty() || !locked); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        locked = rx.status().locked();
        iq = rx.constellation(&supported);
    }
    CHECK(locked);
    CHECK(supported);
    CHECK(iq.size() == 1024);
    // 8PSK: points on a circle of radius ~7000
    double meanR = 0;
    for (const auto &s : iq)
        meanR += std::hypot(s.i, s.q);
    meanR /= iq.empty() ? 1 : static_cast<double>(iq.size());
    CHECK(meanR > 5500 && meanR < 8500);
    CHECK(rx.stats().bytesIn > 0);

    // Retune to an empty frequency: lock lost, samples cleared
    TuningParams t = cfg.tuning;
    t.frequencyMHz = 11300;
    rx.retune(t);
    for (int i = 0; i < 100 && rx.status().locked(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(!rx.status().locked());
    CHECK(rx.constellation().empty());
    rx.stop();

    // A stock driver: reported as unsupported, the receiver keeps running
    setenv("FAKE_DVB_NEUMO", "0", 1);
    Receiver rx2;
    rx2.setStatusInterval(50);
    rx2.setConstellationSamples(256);
    CHECK(rx2.start(cfg));
    for (int i = 0; i < 100 && supported; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rx2.constellation(&supported);
    }
    CHECK(!supported);
    CHECK(rx2.isRunning());
    rx2.stop();
    unsetenv("FAKE_DVB_NEUMO");
}

int main()
{
    if (!std::getenv("LD_PRELOAD")) {
        std::printf("Skipped: needs the fake frontend (LD_PRELOAD=libtbs-fake-frontend.so)\n");
        return 0;
    }
    const std::string ts = (std::filesystem::temp_directory_path() / "tbs_scan_test.ts").string();
    {
        std::ofstream f(ts, std::ios::binary);
        unsigned char pkt[188] = {0x47, 0x1F, 0xFF, 0x10};
        for (int i = 0; i < 2000; ++i)
            f.write(reinterpret_cast<const char *>(pkt), sizeof(pkt));
    }

    testCapabilities();
    testSpectrum();
    testBlindScan();
    testStockDriver();
    testConstellationAndTuning(ts);
    std::filesystem::remove(ts);

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All scan tests passed\n");
    return 0;
}
