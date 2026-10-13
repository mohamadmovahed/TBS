#include "tbsbbproc/NetworkScan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace tbs {
namespace bb {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t)
{
    return std::chrono::duration<double>(Clock::now() - t).count();
}

bool hasTable(const TsReport &ts, uint8_t tableId)
{
    for (const TableStat &t : ts.tables)
        if (t.tableId == tableId && t.sections > 0)
            return true;
    return false;
}

// The stream of the snapshot that carries the transport stream for this tuning
const StreamReport *pickStream(const ProcessorSnapshot &s, const TuningParams &t)
{
    const StreamReport *best = nullptr;
    for (const StreamReport &r : s.streams) {
        const bool ts = r.content == StreamContent::TransportStream || r.content == StreamContent::TsInput;
        if (!ts)
            continue;
        if (t.inputStreamId >= 0 && r.isi == t.inputStreamId)
            return &r;
        if (!best || r.ts.patSeen > best->ts.patSeen)
            best = &r;
    }
    return best;
}

std::string pol(const TuningParams &t)
{
    return t.polarization == Polarization::VerticalRight13V ? "V" : t.polarization == Polarization::Off ? "-" : "H";
}

} // namespace

TuningParams tuningFromDelivery(const SatelliteDelivery &sd, const TuningParams &lnb)
{
    TuningParams t = lnb;
    t.frequencyMHz = sd.frequencyMHz;
    t.symbolRateKsps = sd.symbolRateKsps;
    t.polarization = (sd.polarization == 1 || sd.polarization == 3) ? Polarization::VerticalRight13V
                                                                     : Polarization::HorizontalLeft18V;
    t.deliverySystem = sd.s2 ? DeliverySystem::DvbS2 : DeliverySystem::DvbS;
    t.modulation = sd.modulation == 1 ? ModulationHint::Qpsk : sd.modulation == 2 ? ModulationHint::Psk8 : ModulationHint::Auto;
    t.inputStreamId = sd.inputStreamId;
    t.plsMode = PlsMode::Gold;
    t.plsCode = sd.hasPls ? sd.plsGold : 0;
    return t;
}

std::vector<TuningParams> tuningFromNetwork(const NetworkInfo &nit, const TuningParams &lnb)
{
    std::vector<TuningParams> out;
    for (const TransportInfo &t : nit.transports)
        if (t.sat.valid && t.sat.frequencyMHz > 0 && t.sat.symbolRateKsps > 0)
            out.push_back(tuningFromDelivery(t.sat, lnb));
    return out;
}

bool sameTransponder(const TuningParams &a, const TuningParams &b, double toleranceMHz)
{
    return std::fabs(a.frequencyMHz - b.frequencyMHz) <= toleranceMHz && a.polarization == b.polarization
        && a.inputStreamId == b.inputStreamId;
}

size_t NetworkScanResult::serviceCount() const
{
    size_t n = 0;
    for (const auto &t : transponders)
        n += t.services.size();
    return n;
}

NetworkScanResult runNetworkScan(Receiver &rx, Processor &proc, const std::vector<TuningParams> &seeds,
                                 const NetworkScanOptions &opt,
                                 const std::function<void(const NetworkScanProgress &)> &progress,
                                 const std::function<void(const ScannedTransponder &)> &onTransponder,
                                 const std::atomic<bool> *cancel)
{
    NetworkScanResult result;
    struct Pending
    {
        TuningParams tuning;
        std::string delivery;
    };
    std::vector<Pending> queue;
    for (const TuningParams &t : seeds)
        queue.push_back({t, {}});

    bool haveOrbital = false;
    double orbital = 0.0;
    bool east = true;
    auto cancelled = [&]() { return cancel && cancel->load(); };
    auto sleepMs = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

    for (size_t i = 0; i < queue.size() && static_cast<int>(i) < opt.maxTransponders; ++i) {
        if (cancelled()) {
            result.cancelled = true;
            break;
        }
        ScannedTransponder tp;
        tp.tuning = queue[i].tuning;
        tp.delivery = queue[i].delivery;
        const auto t0 = Clock::now();
        if (progress) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Tuning %.3f MHz %s %.0f kS/s", tp.tuning.frequencyMHz, pol(tp.tuning).c_str(),
                          tp.tuning.symbolRateKsps);
            progress({static_cast<int>(i), static_cast<int>(queue.size()), msg});
        }
        if (i > 0 || !(rx.config().tuning.frequencyMHz == tp.tuning.frequencyMHz
                       && rx.config().tuning.polarization == tp.tuning.polarization
                       && rx.config().tuning.inputStreamId == tp.tuning.inputStreamId))
            rx.retune(tp.tuning);

        // The status is refreshed periodically: give the demodulator and the status time to follow
        sleepMs(700);
        while (!rx.status().locked() && secondsSince(t0) * 1000 < opt.lockTimeoutMs && !cancelled() && rx.isRunning())
            sleepMs(100);
        tp.locked = rx.status().locked();
        if (!rx.isRunning())
            break;

        if (tp.locked) {
            proc.reset();
            const auto tl = Clock::now();
            const StreamReport *sr = nullptr;
            ProcessorSnapshot snap;
            while (!cancelled()) {
                sleepMs(200);
                snap = proc.snapshot();
                sr = pickStream(snap, tp.tuning);
                if (sr && sr->ts.patSeen && hasTable(sr->ts, 0x42) && (!opt.followNit || hasTable(sr->ts, 0x40))) {
                    tp.tablesComplete = true;
                    break;
                }
                if (secondsSince(tl) * 1000 >= opt.tableTimeoutMs)
                    break;
            }
            if (sr) {
                const TsReport &ts = sr->ts;
                tp.tsid = ts.tsid;
                tp.onid = ts.onid;
                for (const ServiceInfo &s : ts.services)
                    if (s.actual && (s.tsid == ts.tsid || ts.tsid == 0))
                        tp.services.push_back(s);
                for (const NetworkInfo &n : ts.networks) {
                    if (n.actual) {
                        tp.networkId = n.networkId;
                        tp.networkName = n.name;
                        if (result.networkName.empty()) {
                            result.networkId = n.networkId;
                            result.networkName = n.name;
                        }
                    }
                    if (!opt.followNit || (!n.actual && !opt.includeNitOther))
                        continue;
                    for (const TransportInfo &t : n.transports) {
                        if (!t.sat.valid || t.sat.frequencyMHz <= 0 || t.sat.symbolRateKsps <= 0)
                            continue;
                        if (opt.sameOrbitalPosition) {
                            if (!haveOrbital) {
                                haveOrbital = true;
                                orbital = t.sat.orbitalPositionDeg;
                                east = t.sat.east;
                            } else if (std::fabs(t.sat.orbitalPositionDeg - orbital) > 0.25 || t.sat.east != east) {
                                continue;
                            }
                        }
                        const TuningParams nt = tuningFromDelivery(t.sat, tp.tuning);
                        // The transponder being scanned: keep its NIT description
                        if (sameTransponder(nt, tp.tuning) && tp.delivery.empty())
                            tp.delivery = t.delivery;
                        const bool known = std::any_of(queue.begin(), queue.end(),
                                                       [&](const Pending &p) { return sameTransponder(p.tuning, nt); });
                        if (!known)
                            queue.push_back({nt, t.delivery});
                    }
                }
            }
        }
        tp.seconds = secondsSince(t0);
        result.transponders.push_back(tp);
        if (onTransponder)
            onTransponder(tp);
    }
    if (cancelled())
        result.cancelled = true;
    if (progress)
        progress({static_cast<int>(result.transponders.size()), static_cast<int>(queue.size()),
                  result.cancelled ? "Cancelled" : "Done"});
    return result;
}

std::string formatNetworkScan(const NetworkScanResult &r)
{
    std::string out;
    char line[256];
    if (!r.networkName.empty() || r.networkId) {
        std::snprintf(line, sizeof(line), "Network %u \"%s\"\n", r.networkId, r.networkName.c_str());
        out += line;
    }
    for (const ScannedTransponder &t : r.transponders) {
        std::snprintf(line, sizeof(line), "%.3f MHz %s %.0f kS/s%s: ", t.tuning.frequencyMHz, pol(t.tuning).c_str(),
                      t.tuning.symbolRateKsps,
                      t.tuning.inputStreamId >= 0 ? (" ISI " + std::to_string(t.tuning.inputStreamId)).c_str() : "");
        out += line;
        if (!t.locked) {
            out += "no lock\n";
            continue;
        }
        std::snprintf(line, sizeof(line), "TSID %u ONID %u, %zu services%s (%.1f s)\n", t.tsid, t.onid, t.services.size(),
                      t.tablesComplete ? "" : ", tables incomplete", t.seconds);
        out += line;
        for (const ServiceInfo &s : t.services) {
            std::snprintf(line, sizeof(line), "    %5u  %-28s %-20s %s%s\n", s.serviceId, s.name.c_str(), s.provider.c_str(),
                          s.typeName.c_str(), s.scrambled ? ", scrambled" : "");
            out += line;
        }
    }
    std::snprintf(line, sizeof(line), "%zu transponders, %zu services%s\n", r.transponders.size(), r.serviceCount(),
                  r.cancelled ? " (cancelled)" : "");
    out += line;
    return out;
}

} // namespace bb
} // namespace tbs
