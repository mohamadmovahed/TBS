#include "tbsdvb/Scan.h"

#include "Util.h"
#include "tbsdvb/DvbNames.h"

#if defined(TBSDVB_HAVE_LINUX_DVB)
#include "platform/LinuxDvbDevice.h"
#endif

#include <algorithm>
#include <cmath>

namespace tbs {

std::string BlindScanCarrier::describe() const
{
    std::string s = util::format("%.3f MHz %c %.0f kS/s %s", frequencyMHz,
                                 tuning.polarization == Polarization::VerticalRight13V ? 'V' : 'H', symbolRateKsps,
                                 deliverySystem == DeliverySystem::DvbS ? "DVB-S" : "DVB-S2");
    if (modulation >= 0)
        s += " " + DvbNames::modulation(modulation);
    if (fec >= 0)
        s += " " + DvbNames::fec(fec);
    if (isis.size() > 1) {
        s += util::format(", %zu streams (ISI", isis.size());
        for (size_t i = 0; i < isis.size() && i < 8; ++i)
            s += util::format(" %d", isis[i]);
        s += isis.size() > 8 ? " ...)" : ")";
    }
    if (hasCnrDb)
        s += util::format(", C/N %.1f dB", cnrDb);
    return s;
}

namespace Scan {

std::vector<BandSegment> bandSegments(const TuningParams &lnb, double startMHz, double endMHz)
{
    std::vector<BandSegment> out;
    if (endMHz < startMHz)
        std::swap(startMHz, endMHz);
    if (endMHz - startMHz < 1e-6)
        return out;

    auto add = [&](double a, double b, bool high) {
        if (b - a < 1e-6)
            return;
        BandSegment seg;
        seg.startMHz = a;
        seg.endMHz = b;
        seg.tuning = lnb;
        seg.tuning.frequencyMHz = high ? std::max(a, lnb.lnbSwitchMHz) : a;
        seg.tuning.tone = lnb.lnbType == LnbType::Universal ? (high ? ToneMode::On : ToneMode::Off) : lnb.tone;
        seg.tuning.inputStreamId = lnb.inputStreamId;
        const double lo = seg.tuning.activeLoMHz();
        double ifA = lnb.lnbType == LnbType::Direct ? a : std::fabs(a - lo);
        double ifB = lnb.lnbType == LnbType::Direct ? b : std::fabs(b - lo);
        if (ifA > ifB)
            std::swap(ifA, ifB);
        seg.ifStartKHz = static_cast<uint32_t>(std::lround(ifA * 1000.0));
        seg.ifEndKHz = static_cast<uint32_t>(std::lround(ifB * 1000.0));
        out.push_back(seg);
    };

    if (lnb.lnbType == LnbType::Universal && startMHz < lnb.lnbSwitchMHz && endMHz > lnb.lnbSwitchMHz) {
        add(startMHz, lnb.lnbSwitchMHz, false);
        add(lnb.lnbSwitchMHz, endMHz, true);
    } else {
        add(startMHz, endMHz, lnb.lnbType == LnbType::Universal && startMHz >= lnb.lnbSwitchMHz);
    }
    return out;
}

double ifToRfMHz(const TuningParams &t, double ifKHz)
{
    const double ifMHz = ifKHz / 1000.0;
    if (t.lnbType == LnbType::Direct)
        return ifMHz;
    const double lo = t.activeLoMHz();
    // An LO above the band (C band) inverts the spectrum
    return lo > t.frequencyMHz ? lo - ifMHz : lo + ifMHz;
}

std::vector<SpectrumPeak> findPeaks(const std::vector<SpectrumPoint> &pts, double minRiseDb, double minSymbolRateKsps)
{
    std::vector<SpectrumPeak> peaks;
    const size_t n = pts.size();
    if (n < 5)
        return peaks;

    // Light smoothing against measurement noise
    std::vector<double> lv(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t a = i > 0 ? i - 1 : i;
        const size_t b = i + 1 < n ? i + 1 : i;
        lv[i] = (pts[a].level + 2.0 * pts[i].level + pts[b].level) / 4.0;
    }
    // Noise floor: 20th percentile
    std::vector<double> sorted = lv;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 5), sorted.end());
    const double floor = sorted[n / 5];
    const double threshold = floor + minRiseDb;

    // Splits [a, b] at the deepest dip that is minRiseDb below the maxima on both sides
    std::function<void(size_t, size_t)> region = [&](size_t a, size_t b) {
        size_t top = a;
        for (size_t i = a; i <= b; ++i)
            if (lv[i] > lv[top])
                top = i;
        // Look for a dip separating two carriers
        size_t bestDip = 0;
        double bestDepth = 0.0;
        double leftMax = lv[a];
        for (size_t i = a + 1; i < b; ++i) {
            leftMax = std::max(leftMax, lv[i - 1]);
            double rightMax = lv[i + 1];
            for (size_t j = i + 1; j <= b; ++j)
                rightMax = std::max(rightMax, lv[j]);
            const double depth = std::min(leftMax, rightMax) - lv[i];
            if (depth >= minRiseDb && depth > bestDepth) {
                bestDepth = depth;
                bestDip = i;
            }
        }
        if (bestDip > a && bestDip < b) {
            region(a, bestDip);
            region(bestDip, b);
            return;
        }
        // 3 dB edges around the maximum, linearly interpolated
        const double half = lv[top] - 3.0;
        size_t l = top;
        while (l > a && lv[l - 1] >= half)
            --l;
        size_t r = top;
        while (r < b && lv[r + 1] >= half)
            ++r;
        auto cross = [&](size_t in, size_t outIdx) {
            const double d = lv[in] - lv[outIdx];
            const double f = d > 1e-9 ? (lv[in] - half) / d : 0.0;
            return pts[in].frequencyMHz + f * (pts[outIdx].frequencyMHz - pts[in].frequencyMHz);
        };
        const double fl = l > a ? cross(l, l - 1) : pts[l].frequencyMHz;
        const double fr = r < b ? cross(r, r + 1) : pts[r].frequencyMHz;
        SpectrumPeak p;
        p.frequencyMHz = (fl + fr) / 2.0;
        p.symbolRateKsps = (fr - fl) * 1000.0; // 3 dB bandwidth ~ symbol rate
        p.level = lv[top];
        p.snrDb = lv[top] - floor;
        if (p.symbolRateKsps >= minSymbolRateKsps)
            peaks.push_back(p);
    };

    size_t i = 0;
    while (i < n) {
        if (lv[i] < threshold) {
            ++i;
            continue;
        }
        const size_t a = i;
        while (i + 1 < n && lv[i + 1] >= threshold)
            ++i;
        region(a, i);
        ++i;
    }
    return peaks;
}

namespace {

bool runSegments(const std::vector<BandSegment> &segs, const ProgressFn &progress, std::string *error,
                 const std::function<bool(size_t, const BandSegment &, const std::function<bool()> &)> &fn)
{
    if (segs.empty()) {
        if (error)
            *error = "Empty frequency range";
        return false;
    }
    bool cancelled = false;
    for (size_t k = 0; k < segs.size(); ++k) {
        if (progress && !progress(static_cast<double>(k) / segs.size(),
                                  util::format("%.1f-%.1f MHz", segs[k].startMHz, segs[k].endMHz))) {
            cancelled = true;
            break;
        }
        auto isCancelled = [&]() { return progress && !progress(-1.0, std::string()); };
        if (!fn(k, segs[k], isCancelled))
            return false;
    }
    if (cancelled) {
        if (error)
            *error = "Cancelled";
        return false;
    }
    if (progress)
        progress(1.0, "Done");
    return true;
}

} // namespace

ScanCapabilities capabilities(const DeviceSelection &sel)
{
    ScanCapabilities caps;
#if defined(TBSDVB_HAVE_LINUX_DVB)
    if (sel.kind == DeviceKind::LinuxDvb) {
        LinuxDvbDevice dev;
        std::string err;
        if (!dev.openFrontend(sel, &err)) {
            caps.note = err;
            return caps;
        }
        return dev.scanCapabilities();
    }
#endif
    (void)sel;
    caps.note = "Spectrum analysis and blind scan need a Linux DVB frontend with the neumoDVB driver";
    return caps;
}

bool spectrum(const DeviceSelection &sel, const SpectrumRequest &req, SpectrumResult *out, std::string *error,
              const ProgressFn &progress)
{
    *out = SpectrumResult();
    out->polarization = req.lnb.polarization;
#if defined(TBSDVB_HAVE_LINUX_DVB)
    if (sel.kind == DeviceKind::LinuxDvb) {
        LinuxDvbDevice dev;
        if (!dev.openFrontend(sel, error))
            return false;
        const bool ok = runSegments(bandSegments(req.lnb, req.startMHz, req.endMHz), progress, error,
                                    [&](size_t, const BandSegment &seg, const std::function<bool()> &cancelled) {
                                        return dev.spectrumSegment(seg, req, out, error, cancelled);
                                    });
        std::sort(out->points.begin(), out->points.end(),
                  [](const SpectrumPoint &a, const SpectrumPoint &b) { return a.frequencyMHz < b.frequencyMHz; });
        if (out->peaks.empty())
            out->peaks = findPeaks(out->points);
        std::sort(out->peaks.begin(), out->peaks.end(),
                  [](const SpectrumPeak &a, const SpectrumPeak &b) { return a.frequencyMHz < b.frequencyMHz; });
        return ok;
    }
#endif
    (void)sel;
    (void)progress;
    if (error)
        *error = "Spectrum analysis needs a Linux DVB frontend with the neumoDVB driver";
    return false;
}

bool blindScan(const DeviceSelection &sel, const BlindScanRequest &req, std::vector<BlindScanCarrier> *out,
               std::string *error, const ProgressFn &progress, const std::function<void(const BlindScanCarrier &)> &onCarrier,
               SpectrumResult *spectrumOut)
{
    out->clear();
    if (spectrumOut) {
        *spectrumOut = SpectrumResult();
        spectrumOut->polarization = req.lnb.polarization;
    }
#if defined(TBSDVB_HAVE_LINUX_DVB)
    if (sel.kind == DeviceKind::LinuxDvb) {
        LinuxDvbDevice dev;
        if (!dev.openFrontend(sel, error))
            return false;
        const std::vector<BandSegment> segs = bandSegments(req.lnb, req.startMHz, req.endMHz);
        const double total = req.endMHz - req.startMHz;
        const bool ok = runSegments(segs, progress, error, [&](size_t, const BandSegment &seg, const std::function<bool()> &cancelled) {
            return dev.blindScanSegment(seg, req, out, spectrumOut, error, cancelled,
                                        [&](const BlindScanCarrier &c, double) {
                                            if (progress && total > 0)
                                                progress(std::clamp((c.frequencyMHz - req.startMHz) / total, 0.0, 1.0),
                                                         "Found " + c.describe());
                                            if (onCarrier)
                                                onCarrier(c);
                                        });
        });
        std::sort(out->begin(), out->end(),
                  [](const BlindScanCarrier &a, const BlindScanCarrier &b) { return a.frequencyMHz < b.frequencyMHz; });
        if (spectrumOut)
            std::sort(spectrumOut->points.begin(), spectrumOut->points.end(),
                      [](const SpectrumPoint &a, const SpectrumPoint &b) { return a.frequencyMHz < b.frequencyMHz; });
        return ok;
    }
#endif
    (void)sel;
    (void)progress;
    (void)onCarrier;
    if (error)
        *error = "Blind scan needs a Linux DVB frontend with the neumoDVB driver";
    return false;
}

} // namespace Scan
} // namespace tbs
