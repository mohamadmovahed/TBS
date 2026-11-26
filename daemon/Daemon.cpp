#include "Daemon.h"

#include "Dashboard.h"

#include <tbsdvb/Device.h>
#include <tbsdvb/DvbNames.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using json::Value;

namespace {

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const char *stateName(tbs::ReceiverState s)
{
    switch (s) {
    case tbs::ReceiverState::Stopped: return "stopped";
    case tbs::ReceiverState::Starting: return "starting";
    case tbs::ReceiverState::Running: return "running";
    case tbs::ReceiverState::Stopping: return "stopping";
    }
    return "?";
}

Value optional(bool has, double v)
{
    return has && std::isfinite(v) ? Value(v) : Value();
}

Value eventJson(const tbs::bb::EventInfo &e)
{
    Value o = Value::object();
    o["id"] = e.eventId;
    o["start"] = static_cast<long long>(e.startUtc);
    o["duration"] = e.durationSec;
    o["name"] = e.name;
    o["text"] = e.text;
    if (!e.extendedText.empty())
        o["extendedText"] = e.extendedText;
    if (!e.items.empty()) {
        Value items = Value::array();
        for (const auto &it : e.items) {
            Value i = Value::object();
            i["description"] = it.first;
            i["value"] = it.second;
            items.push(i);
        }
        o["items"] = items;
    }
    if (!e.genre.empty())
        o["genre"] = e.genre;
    if (e.parentalAge)
        o["parentalAge"] = e.parentalAge;
    o["language"] = e.language;
    o["runningStatus"] = e.runningStatus;
    return o;
}

} // namespace

Daemon::Daemon(DaemonConfig cfg, std::string configPath)
    : m_cfg(std::move(cfg))
    , m_configPath(std::move(configPath))
    , m_monitor(m_cfg.monitor)
{
    m_monitor.setAlarmCallback([this](const tbs::AlarmEvent &e) {
        Value m = Value::object();
        m["type"] = "alarm";
        m["alarm"] = tbs::alarmTypeName(e.type);
        m["title"] = tbs::alarmTypeTitle(e.type);
        m["state"] = e.raised ? "raised" : "cleared";
        m["message"] = e.message;
        m["time"] = static_cast<long long>(e.timeMs);
        m_server.broadcast(m.dump());
        log(std::string(e.raised ? "ALARM " : "Alarm cleared: ") + tbs::alarmTypeTitle(e.type) + " (" + e.message + ")");
    });
    m_server.setHandler([this](const http::Request &r) { return handle(r); });
    m_server.setWebSocket("/api/v1/ws", [this](const http::Request &r) { return authorized(r); },
                          [this](int id) {
                              Value s = statusJson();
                              s["type"] = "status";
                              m_server.sendTo(id, s.dump());
                          });
}

Daemon::~Daemon()
{
    cancelScan();
    stopServer();
    stopReceiver();
}

void Daemon::log(const std::string &message)
{
    const int64_t t = nowMs();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_log.emplace_back(t, message);
        while (m_log.size() > 1000)
            m_log.pop_front();
    }
    std::fprintf(stderr, "%s\n", message.c_str());
    Value m = Value::object();
    m["type"] = "log";
    m["time"] = static_cast<long long>(t);
    m["message"] = message;
    m_server.broadcast(m.dump());
}

bool Daemon::startServer(std::string *error)
{
    std::string host = m_cfg.listen;
    uint16_t port = 8080;
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = static_cast<uint16_t>(std::atoi(host.substr(colon + 1).c_str()));
        host = host.substr(0, colon);
    }
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    if (!m_server.listen(host, port, error))
        return false;
    log("API and dashboard on http://" + (host.empty() ? std::string("0.0.0.0") : host) + ":"
        + std::to_string(m_server.port()) + "/" + (m_cfg.token.empty() ? " (no token: anyone who can reach it has full control)" : ""));
    return true;
}

void Daemon::stopServer()
{
    m_server.stop();
}

bool Daemon::startReceiver(std::string *error)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_scan.running && m_scan.kind != "network") {
            const std::string err = "a " + m_scan.kind + " scan uses the frontend (POST /api/v1/scan/cancel)";
            if (error)
                *error = err;
            return false;
        }
    }
    stopReceiver();
    DaemonConfig cfg;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cfg = m_cfg;
        m_wanted = true;
        m_stoppedAtMs = 0;
        m_lastError.clear();
    }
    std::shared_ptr<tbs::bb::Processor> proc;
    if (cfg.processingEnabled) {
        proc = std::make_shared<tbs::bb::Processor>(cfg.processing);
        std::string err;
        if (!proc->open(&err)) {
            log("ERROR: " + err);
            if (error)
                *error = err;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastError = err;
            m_stoppedAtMs = nowMs();
            return false;
        }
        const auto first = proc->snapshot();
        if (!first.tunWarning.empty())
            log("WARNING: " + first.tunWarning);
    }
    auto rx = std::make_unique<tbs::Receiver>();
    tbs::Receiver *raw = rx.get();
    tbs::Receiver::Callbacks cb;
    cb.onLog = [this](const std::string &m) { log(m); };
    cb.onStateChanged = [this](tbs::ReceiverState s, const std::string &err) {
        if (s == tbs::ReceiverState::Stopped) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!err.empty())
                m_lastError = err;
            m_stoppedAtMs = nowMs();
        }
        Value m = Value::object();
        m["type"] = "state";
        m["state"] = stateName(s);
        m["error"] = err;
        m_server.broadcast(m.dump());
    };
    cb.onStats = [this, raw](const tbs::StreamStats &st) { m_monitor.update(raw->state(), raw->status(), st); };
    if (proc) {
        if (cfg.receiver.capture == tbs::CaptureMode::BBFrame)
            cb.onBbFrame = [proc](const uint8_t *f, size_t n, const tbs::BbHeader &h) { proc->pushBbFrame(f, n, h); };
        else
            cb.onTsPackets = [proc](const uint8_t *p, size_t n) { proc->pushTs(p, n); };
    }
    rx->setCallbacks(cb);
    rx->setStatusInterval(1000); // one history sample per second
    rx->setPipelineBufferSize(static_cast<size_t>(std::max<uint32_t>(cfg.pipelineBufferMB, 8)) << 20);
    m_monitor.setConfig(cfg.monitor);
    m_monitor.reset();
    if (!rx->start(cfg.receiver)) {
        const std::string err = rx->lastError().empty() ? std::string("cannot start the receiver") : rx->lastError();
        if (error)
            *error = err;
        log("ERROR: " + err);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = err;
        m_stoppedAtMs = nowMs(); // retried after restartDelaySec
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rx = std::move(rx);
    m_proc = proc;
    m_startedMs = nowMs();
    return true;
}

void Daemon::stopReceiver()
{
    bool networkScan = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        networkScan = m_scan.running && m_scan.kind == "network";
    }
    if (networkScan) // it uses the receiver
        cancelScan();
    std::unique_ptr<tbs::Receiver> rx;
    std::shared_ptr<tbs::bb::Processor> proc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_wanted = false;
        rx = std::move(m_rx);
        proc = m_proc;
    }
    if (rx)
        rx->stop();
    if (proc)
        proc->close();
}

void Daemon::tick()
{
    bool restart = false;
    std::string why;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool running = m_rx && m_rx->isRunning();
        const bool frontendScan = m_scan.running && m_scan.kind != "network";
        if (m_wanted && !running && !frontendScan && m_cfg.restartDelaySec > 0 && m_stoppedAtMs
            && nowMs() - m_stoppedAtMs >= static_cast<int64_t>(m_cfg.restartDelaySec) * 1000)
            restart = true;
        why = m_lastError;
    }
    if (restart) {
        log("Restarting the receiver" + (why.empty() ? std::string() : " after: " + why));
        startReceiver();
    }
    if (m_server.webSocketClients() > 0) {
        Value s = statusJson();
        s["type"] = "status";
        m_server.broadcast(s.dump());
    }
}

bool Daemon::authorized(const http::Request &req) const
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_cfg.token;
    }
    if (token.empty())
        return true;
    const std::string auth = req.header("authorization");
    if (auth == "Bearer " + token)
        return true;
    return req.param("token") == token; // WebSocket from a browser cannot set headers
}

bool Daemon::saveConfig(std::string *error) const
{
    if (m_configPath.empty())
        return true;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        text = toJson(m_cfg).dump(2) + "\n";
    }
    const std::string tmp = m_configPath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << text)) {
            if (error)
                *error = "cannot write " + tmp;
            return false;
        }
    }
    std::remove(m_configPath.c_str()); // rename() does not replace on Windows
    if (std::rename(tmp.c_str(), m_configPath.c_str()) != 0) {
        if (error)
            *error = "cannot replace " + m_configPath;
        return false;
    }
    return true;
}

Value Daemon::statusJson() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Value o = Value::object();
    o["time"] = static_cast<long long>(nowMs());
    o["state"] = m_rx ? stateName(m_rx->state()) : "stopped";
    o["error"] = m_lastError;
    o["wanted"] = m_wanted;
    o["uptimeSec"] = m_rx && m_rx->isRunning() ? (nowMs() - m_startedMs) / 1000.0 : 0.0;
    o["tuning"] = toJson(m_cfg.receiver.tuning);
    o["tuningText"] = m_cfg.receiver.tuning.describe();
    o["capture"] = m_cfg.receiver.capture == tbs::CaptureMode::BBFrame ? "bbframe" : "ts";
    if (!m_rx)
        return o;

    const tbs::FrontendStatus fe = m_rx->status();
    Value f = Value::object();
    f["valid"] = fe.valid;
    f["locked"] = fe.locked();
    f["flags"] = tbs::DvbNames::statusFlags(fe.flags);
    f["levelDbm"] = optional(fe.hasSignalDbm, fe.signalDbm);
    f["levelPercent"] = optional(fe.hasSignalPercent, fe.signalPercent);
    f["cnrDb"] = optional(fe.hasCnrDb, fe.cnrDb);
    f["cnrPercent"] = optional(fe.hasCnrPercent, fe.cnrPercent);
    f["ber"] = optional(fe.hasBer, fe.ber);
    f["errorBlocks"] = fe.hasErrorBlocks ? Value(static_cast<unsigned long long>(fe.errorBlocks)) : Value();
    f["temperatureC"] = optional(fe.hasTemperature, fe.temperatureC);
    f["system"] = tbs::DvbNames::deliverySystem(fe.deliverySystem);
    f["modulation"] = tbs::DvbNames::modulation(fe.modulation);
    f["fec"] = tbs::DvbNames::fec(fe.fec);
    f["rolloff"] = tbs::DvbNames::rolloff(fe.rolloff);
    f["pilot"] = tbs::DvbNames::pilot(fe.pilot);
    f["frequencyKHz"] = fe.frequencyKHz;
    f["symbolRate"] = fe.symbolRate;
    f["streamId"] = static_cast<long long>(fe.streamId);
    f["device"] = fe.deviceName;
    o["frontend"] = f;

    const tbs::StreamStats st = m_rx->stats();
    Value s = Value::object();
    s["elapsedSec"] = st.elapsedSec;
    s["bytesIn"] = static_cast<unsigned long long>(st.bytesIn);
    s["inputMbps"] = st.inputMbps;
    s["tsPackets"] = static_cast<unsigned long long>(st.tsPackets);
    s["ccErrors"] = static_cast<unsigned long long>(st.ccErrors);
    s["teiErrors"] = static_cast<unsigned long long>(st.teiErrors);
    s["syncLosses"] = static_cast<unsigned long long>(st.syncLosses);
    s["driverOverflows"] = static_cast<unsigned long long>(st.driverOverflows);
    s["bbFrames"] = static_cast<unsigned long long>(st.bbFrames);
    s["bbFramesPerSec"] = st.bbFramesPerSec;
    s["bbPayloadMbps"] = st.bbPayloadMbps;
    s["bbCrcErrors"] = static_cast<unsigned long long>(st.bbCrcErrors);
    if (st.hasBbHeader)
        s["bbHeader"] = st.lastBbHeader.summary();
    s["pipelineQueuedBytes"] = static_cast<unsigned long long>(st.pipelineQueuedBytes);
    s["pipelineDroppedBytes"] = static_cast<unsigned long long>(st.pipelineDroppedBytes);
    s["fileName"] = st.fileName;
    s["fileBytes"] = static_cast<unsigned long long>(st.fileBytes);
    s["fileStopped"] = st.fileStopped;
    s["zmqMessages"] = static_cast<unsigned long long>(st.zmqMessages);
    s["zmqDropped"] = static_cast<unsigned long long>(st.zmqDropped);
    Value net = Value::array();
    for (const tbs::NetOutputStatus &n : st.network) {
        Value x = Value::object();
        x["url"] = n.url;
        x["state"] = n.state;
        x["active"] = n.active;
        x["peers"] = n.peers;
        x["mbps"] = n.mbps;
        x["packets"] = static_cast<unsigned long long>(n.packets);
        x["sendErrors"] = static_cast<unsigned long long>(n.sendErrors);
        net.push(x);
    }
    s["network"] = net;
    o["stats"] = s;
    o["alarmsActive"] = static_cast<unsigned long long>(m_monitor.activeAlarms());
    return o;
}

Value Daemon::streamsJson()
{
    std::shared_ptr<tbs::bb::Processor> proc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        proc = m_proc;
    }
    Value arr = Value::array();
    if (!proc)
        return arr;
    const auto snap = proc->snapshot();
    for (const auto &st : snap.streams) {
        Value s = Value::object();
        s["isi"] = st.isi;
        s["content"] = tbs::bb::streamContentName(st.content);
        s["mbps"] = st.mbps;
        s["frames"] = static_cast<unsigned long long>(st.frames);
        s["tsPackets"] = static_cast<unsigned long long>(st.tsPackets);
        s["services"] = static_cast<unsigned long long>(st.ts.services.size());
        s["epgEvents"] = static_cast<unsigned long long>(st.ts.epgEvents);
        s["ipMbps"] = st.ip.mbps;
        s["ipPackets"] = static_cast<unsigned long long>(st.ip.ipv4 + st.ip.ipv6);
        s["gsePdus"] = static_cast<unsigned long long>(st.gse.pdus);
        s["mpeDatagrams"] = static_cast<unsigned long long>(st.mpe.datagrams);
        if (st.decoder != tbs::bb::GenericDecoder::None || st.decoderDetected) {
            Value dec = Value::object();
            dec["id"] = tbs::bb::genericDecoderId(st.decoder);
            dec["name"] = tbs::bb::genericDecoderName(st.decoder);
            dec["detected"] = st.decoderDetected;
            dec["probing"] = st.decoder == tbs::bb::GenericDecoder::Auto;
            Value cand = Value::array();
            for (const auto &c : st.decoderScores) {
                Value x = Value::object();
                x["id"] = tbs::bb::genericDecoderId(c.decoder);
                x["coverage"] = c.coverage;
                x["units"] = static_cast<unsigned long long>(c.units);
                cand.push(x);
            }
            dec["candidates"] = cand;
            s["decoder"] = dec;
        }
        s["frameCrc32"] = static_cast<unsigned long long>(st.frameCrc32);
        s["rawIpPackets"] = static_cast<unsigned long long>(st.rawIp.packets);
        s["tsInGsPackets"] = static_cast<unsigned long long>(st.tsExtract.verified);
        arr.push(s);
    }
    return arr;
}

Value Daemon::servicesJson(int isi)
{
    std::shared_ptr<tbs::bb::Processor> proc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        proc = m_proc;
    }
    Value o = Value::object();
    if (!proc)
        return o;
    for (const auto &st : proc->snapshot().streams) {
        if (st.isi != isi && isi != -100)
            continue;
        const tbs::bb::TsReport &t = st.ts;
        o["isi"] = st.isi;
        o["tsid"] = t.tsid;
        o["onid"] = t.onid;
        o["utc"] = t.hasTime ? Value(static_cast<long long>(t.utcTime)) : Value();
        Value services = Value::array();
        for (const auto &p : t.programs) {
            Value s = Value::object();
            s["serviceId"] = p.programNumber;
            s["pmtPid"] = p.pmtPid;
            s["pcrPid"] = p.pcrPid;
            for (const auto &sv : t.services)
                if (sv.actual && sv.serviceId == p.programNumber) {
                    s["name"] = sv.name;
                    s["provider"] = sv.provider;
                    s["type"] = sv.typeName;
                    s["scrambled"] = sv.scrambled;
                    if (sv.present.valid)
                        s["now"] = eventJson(sv.present);
                    if (sv.following.valid)
                        s["next"] = eventJson(sv.following);
                }
            Value es = Value::array();
            for (const auto &e : p.streams) {
                Value x = Value::object();
                x["pid"] = e.pid;
                x["kind"] = e.kind;
                x["codec"] = e.codec;
                x["language"] = e.language;
                x["streamType"] = e.streamType;
                es.push(x);
            }
            s["streams"] = es;
            Value ca = Value::array();
            for (const auto &c : p.ca)
                ca.push(Value(c.systemName));
            s["ca"] = ca;
            services.push(s);
        }
        o["services"] = services;
        Value nets = Value::array();
        for (const auto &n : t.networks) {
            Value x = Value::object();
            x["networkId"] = n.networkId;
            x["name"] = n.name;
            x["actual"] = n.actual;
            nets.push(x);
        }
        o["networks"] = nets;
        break;
    }
    return o;
}

Value Daemon::epgJson(int isi, int serviceId)
{
    std::shared_ptr<tbs::bb::Processor> proc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        proc = m_proc;
    }
    Value arr = Value::array();
    if (!proc)
        return arr;
    for (const auto &s : proc->epg(isi)) {
        if (serviceId >= 0 && s.serviceId != serviceId)
            continue;
        Value o = Value::object();
        o["serviceId"] = s.serviceId;
        o["tsid"] = s.tsid;
        o["onid"] = s.onid;
        o["name"] = s.serviceName;
        o["actual"] = s.actual;
        Value ev = Value::array();
        for (const auto &e : s.events)
            ev.push(eventJson(e));
        o["events"] = ev;
        arr.push(o);
    }
    return arr;
}

Value Daemon::ipJson(int isi)
{
    std::shared_ptr<tbs::bb::Processor> proc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        proc = m_proc;
    }
    Value o = Value::object();
    if (!proc)
        return o;
    const auto snap = proc->snapshot();
    o["pcapFile"] = snap.pcapFile;
    o["pcapPackets"] = static_cast<unsigned long long>(snap.pcapPackets);
    o["tun"] = snap.tunName;
    o["tunPackets"] = static_cast<unsigned long long>(snap.tunPackets);
    for (const auto &st : snap.streams) {
        if (st.isi != isi)
            continue;
        const tbs::bb::IpReport &ip = st.ip;
        o["isi"] = st.isi;
        o["mbps"] = ip.mbps;
        o["packetsPerSec"] = ip.packetsPerSec;
        o["ipv4"] = static_cast<unsigned long long>(ip.ipv4);
        o["ipv6"] = static_cast<unsigned long long>(ip.ipv6);
        o["multicast"] = static_cast<unsigned long long>(ip.multicast);
        o["flowCount"] = static_cast<unsigned long long>(ip.flowCount);
        Value protos = Value::array();
        for (const auto &p : ip.protocols) {
            Value x = Value::object();
            x["name"] = p.name;
            x["packets"] = static_cast<unsigned long long>(p.packets);
            x["bytes"] = static_cast<unsigned long long>(p.bytes);
            protos.push(x);
        }
        o["protocols"] = protos;
        Value flows = Value::array();
        for (const auto &f : ip.topFlows) {
            Value x = Value::object();
            x["protocol"] = f.protocolName;
            x["src"] = f.src;
            x["dst"] = f.dst;
            x["srcPort"] = f.srcPort;
            x["dstPort"] = f.dstPort;
            x["packets"] = static_cast<unsigned long long>(f.packets);
            x["bytes"] = static_cast<unsigned long long>(f.bytes);
            x["mbps"] = f.mbps;
            flows.push(x);
        }
        o["flows"] = flows;
    }
    return o;
}

Value Daemon::alarmsJson() const
{
    Value o = Value::object();
    Value alarms = Value::array();
    for (const tbs::Alarm &a : m_monitor.alarms()) {
        Value x = Value::object();
        x["alarm"] = tbs::alarmTypeName(a.type);
        x["title"] = tbs::alarmTypeTitle(a.type);
        x["enabled"] = a.enabled;
        x["active"] = a.active;
        x["since"] = static_cast<long long>(a.sinceMs);
        x["raisedCount"] = static_cast<unsigned long long>(a.raisedCount);
        x["message"] = a.message;
        alarms.push(x);
    }
    o["alarms"] = alarms;
    Value events = Value::array();
    for (const tbs::AlarmEvent &e : m_monitor.events(200)) {
        Value x = Value::object();
        x["time"] = static_cast<long long>(e.timeMs);
        x["alarm"] = tbs::alarmTypeName(e.type);
        x["state"] = e.raised ? "raised" : "cleared";
        x["message"] = e.message;
        events.push(x);
    }
    o["events"] = events;
    return o;
}

Value Daemon::historyJson(int seconds) const
{
    Value arr = Value::array();
    for (const tbs::MonitorSample &s : m_monitor.history(nowMs() - static_cast<int64_t>(seconds) * 1000)) {
        Value x = Value::object();
        x["t"] = static_cast<long long>(s.timeMs);
        x["locked"] = s.locked;
        x["levelDbm"] = optional(true, s.levelDbm);
        x["cnrDb"] = optional(true, s.cnrDb);
        x["inputMbps"] = s.inputMbps;
        x["payloadMbps"] = s.payloadMbps;
        x["ccErrors"] = s.ccErrors;
        x["bbCrcErrors"] = s.bbCrcErrors;
        arr.push(x);
    }
    return arr;
}

Value Daemon::logJson(size_t lines) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Value arr = Value::array();
    const size_t start = m_log.size() > lines ? m_log.size() - lines : 0;
    for (size_t i = start; i < m_log.size(); ++i) {
        Value x = Value::object();
        x["time"] = static_cast<long long>(m_log[i].first);
        x["message"] = m_log[i].second;
        arr.push(x);
    }
    return arr;
}

http::Response Daemon::handle(const http::Request &req)
{
    using http::Response;
    const std::string &p = req.path;
    if (req.method == "GET" && (p == "/" || p == "/index.html"))
        return Response::text(kDashboardHtml, 200, "text/html; charset=utf-8");
    if (p.rfind("/api/v1/", 0) != 0)
        return Response::error(404, "not found");
    if (!authorized(req))
        return Response::error(401, "missing or wrong token (Authorization: Bearer <token>)");
    const std::string ep = p.substr(8);
    const int isi = std::atoi(req.param("isi", "-1").c_str());

    if (req.method == "GET") {
        if (ep == "status")
            return Response::json(statusJson().dump());
        if (ep == "config") {
            std::lock_guard<std::mutex> lock(m_mutex);
            Value c = toJson(m_cfg);
            if (!m_cfg.token.empty())
                c["api"]["token"] = "***";
            return Response::json(c.dump(2));
        }
        if (ep == "devices") {
            Value arr = Value::array();
            for (const tbs::DeviceInfo &d : tbs::Devices::enumerate(false)) {
                Value x = Value::object();
                x["kind"] = d.kind == tbs::DeviceKind::LinuxDvb ? "linux" : d.kind == tbs::DeviceKind::WindowsBda ? "bda" : "replay";
                x["name"] = d.name;
                x["path"] = d.path;
                x["adapter"] = d.adapter;
                x["frontend"] = d.frontend;
                x["index"] = d.index;
                x["label"] = d.label();
                arr.push(x);
            }
            return Response::json(arr.dump());
        }
        if (ep == "streams")
            return Response::json(streamsJson().dump());
        if (ep == "services")
            return Response::json(servicesJson(req.query.count("isi") ? isi : -100).dump());
        if (ep == "epg")
            return Response::json(epgJson(isi, std::atoi(req.param("service", "-1").c_str())).dump());
        if (ep == "epg.xml") {
            std::shared_ptr<tbs::bb::Processor> proc;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                proc = m_proc;
            }
            std::vector<tbs::bb::ServiceSchedule> all;
            if (proc)
                for (const auto &st : proc->snapshot().streams)
                    for (auto &s : proc->epg(st.isi))
                        all.push_back(std::move(s));
            return Response::text(tbs::bb::formatXmltv(all), 200, "application/xml; charset=utf-8");
        }
        if (ep == "ip")
            return Response::json(ipJson(isi).dump());
        if (ep == "alarms")
            return Response::json(alarmsJson().dump());
        if (ep == "history")
            return Response::json(historyJson(std::max(1, std::atoi(req.param("seconds", "600").c_str()))).dump());
        if (ep == "history.csv") {
            std::string csv = tbs::Monitor::csvHeader();
            const int seconds = std::max(1, std::atoi(req.param("seconds", "86400").c_str()));
            for (const auto &s : m_monitor.history(nowMs() - static_cast<int64_t>(seconds) * 1000))
                csv += tbs::Monitor::csvLine(s);
            Response r = Response::text(csv, 200, "text/csv; charset=utf-8");
            r.headers.emplace_back("Content-Disposition", "attachment; filename=\"tbs-history.csv\"");
            return r;
        }
        if (ep == "log")
            return Response::json(logJson(static_cast<size_t>(std::max(1, std::atoi(req.param("lines", "200").c_str())))).dump());
        if (ep == "scan")
            return Response::json(scanJson(true).dump());
        if (ep == "scan/capabilities")
            return Response::json(capabilitiesJson().dump());
        if (ep == "constellation")
            return Response::json(constellationJson(std::atoi(req.param("samples", "1024").c_str())).dump());
        return Response::error(404, "unknown endpoint");
    }

    if (req.method == "POST" || req.method == "PUT") {
        if (ep == "start" || ep == "restart") {
            std::string err;
            if (!startReceiver(&err))
                return Response::error(409, err);
            return Response::json(statusJson().dump());
        }
        if (ep == "scan/spectrum" || ep == "scan/blind" || ep == "scan/network")
            return startScan(ep.substr(5), req.body);
        if (ep == "scan/cancel") {
            cancelScan();
            return Response::json(scanJson(false).dump());
        }
        if (ep == "stop") {
            stopReceiver();
            log("Stopped through the API");
            return Response::json(statusJson().dump());
        }
        if (ep == "tune") {
            Value body;
            std::string err;
            if (!json::parse(req.body, &body, &err))
                return Response::error(400, "invalid JSON: " + err);
            tbs::TuningParams t;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                t = m_cfg.receiver.tuning;
            }
            if (!tuningFromJson(body, &t, &err))
                return Response::error(400, err);
            bool running = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cfg.receiver.tuning = t;
                running = m_rx && m_rx->isRunning();
                if (running)
                    m_rx->retune(t);
            }
            log("Tuning: " + t.describe());
            if (!saveConfig(&err))
                log("WARNING: " + err);
            return Response::json(statusJson().dump());
        }
        if (ep == "config" && req.method == "PUT") {
            Value body;
            std::string err;
            if (!json::parse(req.body, &body, &err))
                return Response::error(400, "invalid JSON: " + err);
            DaemonConfig next;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                next = m_cfg;
            }
            const std::string oldToken = next.token;
            if (!fromJson(body, &next, &err))
                return Response::error(400, err);
            if (next.token == "***")
                next.token = oldToken; // the redacted value from GET /config
            const bool listenChanged = next.listen != m_cfg.listen;
            bool restart = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cfg = next;
                restart = m_rx && m_rx->isRunning();
            }
            m_monitor.setConfig(next.monitor);
            if (!saveConfig(&err))
                log("WARNING: " + err);
            log("Configuration updated" + std::string(listenChanged ? " (the new listen address applies after a restart of the daemon)" : ""));
            if (restart && !startReceiver(&err))
                return Response::error(409, "configuration saved, but the receiver did not start: " + err);
            return Response::json(statusJson().dump());
        }
        return Response::error(404, "unknown endpoint");
    }
    return Response::error(405, "method not allowed");
}
