// Spectrum, blind scan, NIT network scan and constellation endpoints of the daemon.

#include "Daemon.h"

#include <tbsbbproc/NetworkScan.h>
#include <tbsdvb/DvbNames.h>
#include <tbsdvb/Scan.h>

#include <chrono>
#include <cmath>

using json::Value;

namespace {

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

const char *polName(tbs::Polarization p)
{
    return p == tbs::Polarization::VerticalRight13V ? "V" : p == tbs::Polarization::Off ? "off" : "H";
}

std::vector<tbs::Polarization> polarizationsFrom(const Value &body, tbs::Polarization def)
{
    const std::string p = body.get("polarization").toString(std::string());
    if (p == "both")
        return {tbs::Polarization::HorizontalLeft18V, tbs::Polarization::VerticalRight13V};
    if (p == "v" || p == "V" || p == "r" || p == "R")
        return {tbs::Polarization::VerticalRight13V};
    if (p == "h" || p == "H" || p == "l" || p == "L")
        return {tbs::Polarization::HorizontalLeft18V};
    return {def};
}

Value spectrumJson(const tbs::SpectrumResult &r)
{
    Value o = Value::object();
    o["polarization"] = polName(r.polarization);
    o["decibel"] = r.decibel;
    Value pts = Value::array();
    for (const auto &p : r.points) {
        Value x = Value::array();
        x.push(round3(p.frequencyMHz));
        x.push(std::round(p.level * 100.0) / 100.0);
        pts.push(x);
    }
    o["points"] = pts;
    Value peaks = Value::array();
    for (const auto &p : r.peaks) {
        Value x = Value::object();
        x["frequency"] = round3(p.frequencyMHz);
        x["symbolRate"] = std::round(p.symbolRateKsps);
        x["level"] = std::round(p.level * 10.0) / 10.0;
        x["snr"] = std::round(p.snrDb * 10.0) / 10.0;
        peaks.push(x);
    }
    o["peaks"] = peaks;
    return o;
}

Value carrierJson(const tbs::BlindScanCarrier &c)
{
    Value o = Value::object();
    o["frequency"] = round3(c.frequencyMHz);
    o["polarization"] = polName(c.tuning.polarization);
    o["symbolRate"] = std::round(c.symbolRateKsps * 10.0) / 10.0;
    o["system"] = c.deliverySystem == tbs::DeliverySystem::DvbS ? "DVB-S" : "DVB-S2";
    if (c.modulation >= 0)
        o["modulation"] = tbs::DvbNames::modulation(c.modulation);
    if (c.fec >= 0)
        o["fec"] = tbs::DvbNames::fec(c.fec);
    Value isis = Value::array();
    for (int i : c.isis)
        isis.push(i);
    o["isis"] = isis;
    o["cnr"] = c.hasCnrDb ? Value(std::round(c.cnrDb * 10.0) / 10.0) : Value();
    o["signal"] = c.hasSignalDbm ? Value(std::round(c.signalDbm * 10.0) / 10.0) : Value();
    o["description"] = c.describe();
    o["tuning"] = toJson(c.tuning);
    return o;
}

Value transponderJson(const tbs::bb::ScannedTransponder &t)
{
    Value o = Value::object();
    o["tuning"] = toJson(t.tuning);
    o["locked"] = t.locked;
    o["tablesComplete"] = t.tablesComplete;
    o["tsid"] = t.tsid;
    o["onid"] = t.onid;
    o["delivery"] = t.delivery;
    o["seconds"] = std::round(t.seconds * 10.0) / 10.0;
    Value svcs = Value::array();
    for (const auto &s : t.services) {
        Value x = Value::object();
        x["id"] = s.serviceId;
        x["name"] = s.name;
        x["provider"] = s.provider;
        x["type"] = s.typeName;
        x["scrambled"] = s.scrambled;
        svcs.push(x);
    }
    o["services"] = svcs;
    return o;
}

} // namespace

void Daemon::scanProgress(double fraction, const std::string &message)
{
    Value m = Value::object();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (fraction >= 0)
            m_scan.progress = fraction;
        if (!message.empty())
            m_scan.message = message;
        m["kind"] = m_scan.kind;
        m["progress"] = m_scan.progress;
        m["message"] = m_scan.message;
    }
    m["type"] = "scan";
    m["running"] = true;
    m_server.broadcast(m.dump());
}

Value Daemon::scanJson(bool withResult) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Value o = Value::object();
    o["kind"] = m_scan.kind;
    o["running"] = m_scan.running;
    o["progress"] = m_scan.progress;
    o["message"] = m_scan.message;
    o["error"] = m_scan.error;
    o["started"] = static_cast<long long>(m_scan.startedMs);
    o["finished"] = static_cast<long long>(m_scan.finishedMs);
    if (withResult)
        o["result"] = m_scan.result;
    return o;
}

void Daemon::cancelScan()
{
    m_scanCancel = true;
    if (m_scanThread.joinable() && m_scanThread.get_id() != std::this_thread::get_id())
        m_scanThread.join();
}

Value Daemon::capabilitiesJson()
{
    tbs::DeviceSelection dev;
    bool busy = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dev = m_cfg.receiver.device;
        busy = (m_rx && m_rx->isRunning()) || (m_scan.running && m_scan.kind != "network");
    }
    Value o = Value::object();
    if (busy) {
        // Opening the frontend would fail while it is in use
        o["probed"] = false;
        o["note"] = "The frontend is in use (receiver or scan running); stop it to query the driver";
        return o;
    }
    const tbs::ScanCapabilities c = tbs::Scan::capabilities(dev);
    o["probed"] = c.probed;
    o["neumoApi"] = c.neumoApi;
    o["spectrumSweep"] = c.spectrumSweep;
    o["spectrumFft"] = c.spectrumFft;
    o["blindScan"] = c.blindScan;
    o["constellation"] = c.constellation;
    o["note"] = c.note;
    return o;
}

Value Daemon::constellationJson(int samples)
{
    samples = std::max(16, std::min(samples, 8192));
    Value o = Value::object();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_rx || !m_rx->isRunning()) {
        o["running"] = false;
        o["supported"] = Value();
        o["samples"] = Value::array();
        return o;
    }
    // Reading IQ samples costs a little driver time: enabled on the first request
    m_rx->setConstellationSamples(samples);
    bool supported = true;
    const auto iq = m_rx->constellation(&supported);
    o["running"] = true;
    o["supported"] = supported;
    o["locked"] = m_rx->status().locked();
    Value arr = Value::array();
    for (const auto &s : iq) {
        Value x = Value::array();
        x.push(static_cast<int>(s.i));
        x.push(static_cast<int>(s.q));
        arr.push(x);
    }
    o["samples"] = arr;
    return o;
}

http::Response Daemon::startScan(const std::string &kind, const std::string &bodyText)
{
    using http::Response;
    Value body = Value::object();
    std::string err;
    if (!bodyText.empty() && !json::parse(bodyText, &body, &err))
        return Response::error(400, "invalid JSON: " + err);

    tbs::ReceiverConfig rc;
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_scan.running)
            return Response::error(409, "a " + m_scan.kind + " scan is running (POST /api/v1/scan/cancel)");
        rc = m_cfg.receiver;
        running = m_rx && m_rx->isRunning();
    }
    if (m_scanThread.joinable())
        m_scanThread.join();

    const double start = body.get("start").toNumber(10700.0);
    const double end = body.get("end").toNumber(12750.0);
    if (kind != "network" && !(end > start))
        return Response::error(400, "\"end\" must be above \"start\" (MHz)");
    if (kind != "network" && running)
        return Response::error(409, "the receiver uses the frontend: stop it first (POST /api/v1/stop)");
    if (kind == "network") {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!running)
            return Response::error(409, "the network scan tunes the running receiver: start it first");
        if (!m_proc)
            return Response::error(409, "the network scan needs content processing (\"processing\": {\"enabled\": true})");
    }
    tbs::TuningParams lnb = rc.tuning;
    if (body.has("tuning") && !tuningFromJson(body.get("tuning"), &lnb, &err))
        return Response::error(400, err);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_scan = ScanJob();
        m_scan.kind = kind;
        m_scan.running = true;
        m_scan.startedMs = nowMs();
        m_scan.message = "Starting";
    }
    m_scanCancel = false;
    log("Scan started: " + kind);

    auto progress = [this](double f, const std::string &msg) {
        if (f >= 0 || !msg.empty())
            scanProgress(f, msg);
        return !m_scanCancel.load();
    };
    auto finish = [this, kind](Value result, const std::string &error) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_scan.running = false;
            m_scan.finishedMs = nowMs();
            m_scan.error = error;
            m_scan.result = std::move(result);
            m_scan.progress = error.empty() ? 1.0 : m_scan.progress;
            m_scan.message = error.empty() ? "Done" : error;
        }
        log("Scan " + kind + (error.empty() ? std::string(" finished") : " failed: " + error));
        Value m = scanJson(false);
        m["type"] = "scan";
        m_server.broadcast(m.dump());
    };

    if (kind == "spectrum" || kind == "blind") {
        const auto pols = polarizationsFrom(body, lnb.polarization);
        const std::string method = body.get("method").toString(std::string("fft"));
        const int resolution = body.get("resolution").toInt(kind == "blind" ? 2000 : 0);
        const int fft = body.get("fftSize").toInt(kind == "blind" ? 256 : 512);
        const int maxSr = body.get("maxSymbolRate").toInt(45000);
        const tbs::DeviceSelection dev = rc.device;
        m_scanThread = std::thread([=]() {
            Value result = Value::object();
            Value list = Value::array();
            std::string error;
            for (size_t k = 0; k < pols.size() && error.empty(); ++k) {
                auto partProgress = [&](double f, const std::string &msg) {
                    return progress(f < 0 ? f : (k + f) / pols.size(),
                                    msg.empty() ? msg : std::string(polName(pols[k])) + ": " + msg);
                };
                if (kind == "spectrum") {
                    tbs::SpectrumRequest req;
                    req.startMHz = start;
                    req.endMHz = end;
                    req.lnb = lnb;
                    req.lnb.polarization = pols[k];
                    req.method = method == "sweep" ? tbs::SpectrumMethod::Sweep : tbs::SpectrumMethod::Fft;
                    req.resolutionKHz = resolution;
                    req.fftSize = fft;
                    tbs::SpectrumResult res;
                    if (!tbs::Scan::spectrum(dev, req, &res, &error, partProgress))
                        break;
                    list.push(spectrumJson(res));
                } else {
                    tbs::BlindScanRequest req;
                    req.startMHz = start;
                    req.endMHz = end;
                    req.lnb = lnb;
                    req.lnb.polarization = pols[k];
                    req.resolutionKHz = resolution;
                    req.fftSize = fft;
                    req.maxSymbolRateKsps = maxSr;
                    std::vector<tbs::BlindScanCarrier> found;
                    if (!tbs::Scan::blindScan(dev, req, &found, &error, partProgress,
                                              [&](const tbs::BlindScanCarrier &c) {
                                                  Value m = Value::object();
                                                  m["type"] = "scan";
                                                  m["running"] = true;
                                                  m["kind"] = "blind";
                                                  m["carrier"] = carrierJson(c);
                                                  m_server.broadcast(m.dump());
                                              }))
                        break;
                    for (const auto &c : found)
                        list.push(carrierJson(c));
                }
            }
            result[kind == "spectrum" ? "spectra" : "carriers"] = list;
            finish(result, error);
        });
        return Response::json(scanJson(false).dump(), 202);
    }

    // Network scan on the running receiver
    tbs::bb::NetworkScanOptions opt;
    opt.followNit = body.get("followNit").toBool(true);
    opt.includeNitOther = body.get("nitOther").toBool(false);
    opt.sameOrbitalPosition = !body.get("allPositions").toBool(false);
    opt.tableTimeoutMs = body.get("tableTimeoutMs").toInt(opt.tableTimeoutMs);
    opt.lockTimeoutMs = body.get("lockTimeoutMs").toInt(opt.lockTimeoutMs);
    const tbs::TuningParams original = rc.tuning;
    m_scanThread = std::thread([=]() {
        tbs::Receiver *rx = nullptr;
        std::shared_ptr<tbs::bb::Processor> proc;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            rx = m_rx.get();
            proc = m_proc;
        }
        if (!rx || !proc) {
            finish(Value::object(), "the receiver stopped");
            return;
        }
        const auto r = tbs::bb::runNetworkScan(
            *rx, *proc, {lnb}, opt,
            [&](const tbs::bb::NetworkScanProgress &p) {
                const double f = p.total > 0 ? static_cast<double>(p.index) / p.total : 0.0;
                scanProgress(std::min(f, 1.0), p.message);
            },
            [&](const tbs::bb::ScannedTransponder &t) {
                Value m = Value::object();
                m["type"] = "scan";
                m["running"] = true;
                m["kind"] = "network";
                m["transponder"] = transponderJson(t);
                m_server.broadcast(m.dump());
            },
            &m_scanCancel);
        // Back to the configured transponder
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_rx.get() == rx && rx->isRunning())
                rx->retune(original);
        }
        Value result = Value::object();
        Value net = Value::object();
        net["id"] = r.networkId;
        net["name"] = r.networkName;
        result["network"] = net;
        Value tps = Value::array();
        for (const auto &t : r.transponders)
            tps.push(transponderJson(t));
        result["transponders"] = tps;
        result["services"] = static_cast<unsigned long long>(r.serviceCount());
        finish(result, r.cancelled ? "Cancelled" : std::string());
    });
    return Response::json(scanJson(false).dump(), 202);
}
