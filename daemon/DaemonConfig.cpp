#include "DaemonConfig.h"

#include <utility>
#include <vector>

using json::Value;

namespace {

// Enum <-> name tables
template <typename E>
using Names = std::vector<std::pair<E, const char *>>;

const Names<tbs::DeviceKind> kDevice = {{tbs::DeviceKind::LinuxDvb, "linux"},
                                         {tbs::DeviceKind::WindowsBda, "bda"},
                                         {tbs::DeviceKind::FileReplay, "replay"}};
const Names<tbs::DeliverySystem> kSystem = {{tbs::DeliverySystem::DvbS, "dvbs"}, {tbs::DeliverySystem::DvbS2, "dvbs2"}};
const Names<tbs::ModulationHint> kModulation = {{tbs::ModulationHint::Auto, "auto"},
                                                 {tbs::ModulationHint::Qpsk, "qpsk"},
                                                 {tbs::ModulationHint::Psk8, "8psk"},
                                                 {tbs::ModulationHint::Apsk16, "16apsk"},
                                                 {tbs::ModulationHint::Apsk32, "32apsk"}};
const Names<tbs::LnbType> kLnb = {{tbs::LnbType::Universal, "universal"},
                                   {tbs::LnbType::SingleLo, "single"},
                                   {tbs::LnbType::Direct, "direct"}};
const Names<tbs::Polarization> kPol = {{tbs::Polarization::HorizontalLeft18V, "h"},
                                        {tbs::Polarization::VerticalRight13V, "v"},
                                        {tbs::Polarization::Off, "off"}};
const Names<tbs::ToneMode> kTone = {{tbs::ToneMode::Auto, "auto"}, {tbs::ToneMode::On, "on"}, {tbs::ToneMode::Off, "off"}};
const Names<tbs::PlsMode> kPls = {{tbs::PlsMode::Root, "root"}, {tbs::PlsMode::Gold, "gold"}, {tbs::PlsMode::Combo, "combo"}};
const Names<tbs::CaptureMode> kCapture = {{tbs::CaptureMode::TransportStream, "ts"}, {tbs::CaptureMode::BBFrame, "bbframe"}};
const Names<tbs::BbFileFormat> kBbFile = {{tbs::BbFileFormat::Raw, "raw"},
                                           {tbs::BbFileFormat::B8Marker, "b8"},
                                           {tbs::BbFileFormat::PseudoTs, "pseudo"}};
const Names<tbs::ZmqSocketKind> kZmq = {{tbs::ZmqSocketKind::Pub, "pub"}, {tbs::ZmqSocketKind::Push, "push"}};
const Names<tbs::bb::PcapFormat> kPcap = {{tbs::bb::PcapFormat::Pcap, "pcap"}, {tbs::bb::PcapFormat::PcapNg, "pcapng"}};

template <typename E>
const char *nameOf(const Names<E> &names, E v)
{
    for (const auto &n : names)
        if (n.first == v)
            return n.second;
    return "?";
}

// Readers: leave the target unchanged when the key is missing
struct Reader
{
    const Value &v;
    std::string *error;
    bool ok = true;

    void boolean(const char *key, bool &out)
    {
        const Value &x = v.get(key);
        if (x.isBool())
            out = x.toBool();
        else if (!x.isNull())
            bad(key, "true or false");
    }
    template <typename T>
    void num(const char *key, T &out)
    {
        const Value &x = v.get(key);
        if (x.isNumber())
            out = static_cast<T>(x.toNumber());
        else if (!x.isNull())
            bad(key, "a number");
    }
    void str(const char *key, std::string &out)
    {
        const Value &x = v.get(key);
        if (x.isString())
            out = x.toString();
        else if (!x.isNull())
            bad(key, "a string");
    }
    template <typename E>
    void enumeration(const char *key, const Names<E> &names, E &out)
    {
        const Value &x = v.get(key);
        if (x.isNull())
            return;
        for (const auto &n : names)
            if (x.toString() == n.second) {
                out = n.first;
                return;
            }
        std::string allowed;
        for (const auto &n : names)
            allowed += (allowed.empty() ? "" : ", ") + std::string(n.second);
        bad(key, "one of " + allowed);
    }
    void bad(const char *key, const std::string &what)
    {
        if (ok && error)
            *error = std::string("\"") + key + "\" must be " + what;
        ok = false;
    }
};

} // namespace

Value toJson(const tbs::TuningParams &t)
{
    Value o = Value::object();
    o["frequencyMHz"] = t.frequencyMHz;
    o["symbolRateKsps"] = t.symbolRateKsps;
    o["system"] = nameOf(kSystem, t.deliverySystem);
    o["modulation"] = nameOf(kModulation, t.modulation);
    o["lnb"] = nameOf(kLnb, t.lnbType);
    o["lo1MHz"] = t.lnbLo1MHz;
    o["lo2MHz"] = t.lnbLo2MHz;
    o["switchMHz"] = t.lnbSwitchMHz;
    o["polarization"] = nameOf(kPol, t.polarization);
    o["tone"] = nameOf(kTone, t.tone);
    o["diseqc"] = t.diseqcPort;
    o["isi"] = t.inputStreamId;
    o["plsMode"] = nameOf(kPls, t.plsMode);
    o["plsCode"] = t.plsCode;
    return o;
}

bool tuningFromJson(const Value &v, tbs::TuningParams *t, std::string *error)
{
    if (!v.isObject()) {
        if (error)
            *error = "tuning must be an object";
        return false;
    }
    Reader r{v, error};
    r.num("frequencyMHz", t->frequencyMHz);
    r.num("symbolRateKsps", t->symbolRateKsps);
    r.enumeration("system", kSystem, t->deliverySystem);
    r.enumeration("modulation", kModulation, t->modulation);
    r.enumeration("lnb", kLnb, t->lnbType);
    r.num("lo1MHz", t->lnbLo1MHz);
    r.num("lo2MHz", t->lnbLo2MHz);
    r.num("switchMHz", t->lnbSwitchMHz);
    r.enumeration("polarization", kPol, t->polarization);
    r.enumeration("tone", kTone, t->tone);
    r.num("diseqc", t->diseqcPort);
    r.num("isi", t->inputStreamId);
    r.enumeration("plsMode", kPls, t->plsMode);
    r.num("plsCode", t->plsCode);
    if (r.ok && (t->symbolRateKsps <= 0 || t->frequencyMHz <= 0)) {
        if (error)
            *error = "frequencyMHz and symbolRateKsps must be positive";
        return false;
    }
    return r.ok;
}

Value toJson(const DaemonConfig &c)
{
    const tbs::ReceiverConfig &rc = c.receiver;
    Value root = Value::object();

    Value dev = Value::object();
    dev["kind"] = nameOf(kDevice, rc.device.kind);
    dev["adapter"] = rc.device.adapter;
    dev["frontend"] = rc.device.frontend;
    dev["demux"] = rc.device.demux;
    dev["bdaIndex"] = rc.device.bdaIndex;
    dev["bdaPath"] = rc.device.bdaDevicePath;
    dev["replayFile"] = rc.device.replayFile;
    dev["replayRateMbps"] = rc.device.replayRateMbps;
    dev["replayLoop"] = rc.device.replayLoop;

    Value file = Value::object();
    file["enabled"] = rc.file.enabled;
    file["path"] = rc.file.path;
    file["timestampSuffix"] = rc.file.timestampSuffix;
    file["bbFormat"] = nameOf(kBbFile, rc.file.bbFormat);
    file["splitSizeMB"] = static_cast<unsigned long long>(rc.file.splitSizeMB);
    file["splitMinutes"] = rc.file.splitMinutes;
    file["keepFiles"] = rc.file.keepFiles;
    file["minFreeMB"] = static_cast<unsigned long long>(rc.file.minFreeMB);

    Value zmq = Value::object();
    zmq["enabled"] = rc.zmq.enabled;
    zmq["endpoint"] = rc.zmq.endpoint;
    zmq["socket"] = nameOf(kZmq, rc.zmq.socket);
    zmq["bind"] = rc.zmq.bind;
    zmq["topic"] = rc.zmq.topic;
    zmq["metadataHeader"] = rc.zmq.metadataHeader;
    zmq["tsPacketsPerMessage"] = rc.zmq.tsPacketsPerMessage;
    zmq["sendHwm"] = rc.zmq.sendHwm;

    Value net = Value::array();
    for (const tbs::TsNetworkOutput &o : rc.network) {
        Value n = Value::object();
        n["url"] = tbs::networkUrl(o);
        n["enabled"] = o.enabled;
        net.push(n);
    }

    Value rx = Value::object();
    rx["device"] = dev;
    rx["tuning"] = toJson(rc.tuning);
    rx["capture"] = nameOf(kCapture, rc.capture);
    rx["file"] = file;
    rx["zmq"] = zmq;
    rx["network"] = net;
    rx["bufferMB"] = c.pipelineBufferMB;
    root["receiver"] = rx;

    Value pc = Value::object();
    pc["enabled"] = c.processingEnabled;
    Value pcap = Value::object();
    pcap["enabled"] = c.processing.pcap.enabled;
    pcap["path"] = c.processing.pcap.path;
    pcap["format"] = nameOf(kPcap, c.processing.pcap.format);
    pcap["timestampSuffix"] = c.processing.pcap.timestampSuffix;
    pcap["ethernetFraming"] = c.processing.pcap.ethernetFraming;
    pcap["ipOnly"] = c.processing.pcap.ipOnly;
    pc["pcap"] = pcap;
    Value tun = Value::object();
    tun["enabled"] = c.processing.tun.enabled;
    tun["name"] = c.processing.tun.name;
    tun["tap"] = c.processing.tun.tap;
    pc["tun"] = tun;
    root["processing"] = pc;

    const tbs::MonitorConfig &m = c.monitor;
    Value mon = Value::object();
    mon["historySeconds"] = m.historySeconds;
    mon["noLock"] = m.noLock;
    mon["noLockHoldSec"] = m.noLockHoldSec;
    mon["lowCnr"] = m.lowCnr;
    mon["cnrMinDb"] = m.cnrMinDb;
    mon["cnrHoldSec"] = m.cnrHoldSec;
    mon["lowLevel"] = m.lowLevel;
    mon["levelMinDbm"] = m.levelMinDbm;
    mon["levelHoldSec"] = m.levelHoldSec;
    mon["ccErrors"] = m.ccErrors;
    mon["ccMaxPerWindow"] = m.ccMaxPerWindow;
    mon["bbCrcErrors"] = m.bbCrcErrors;
    mon["bbCrcMaxPerWindow"] = m.bbCrcMaxPerWindow;
    mon["windowSec"] = m.windowSec;
    mon["lowBitrate"] = m.lowBitrate;
    mon["bitrateMinMbps"] = m.bitrateMinMbps;
    mon["bitrateHoldSec"] = m.bitrateHoldSec;
    mon["noData"] = m.noData;
    mon["noDataHoldSec"] = m.noDataHoldSec;
    mon["drops"] = m.drops;
    mon["outputErrors"] = m.outputErrors;
    mon["command"] = m.command;
    root["monitor"] = mon;

    Value api = Value::object();
    api["listen"] = c.listen;
    api["token"] = c.token;
    root["api"] = api;
    root["autostart"] = c.autostart;
    root["restartDelaySec"] = c.restartDelaySec;
    return root;
}

bool fromJson(const Value &v, DaemonConfig *out, std::string *error)
{
    if (!v.isObject()) {
        if (error)
            *error = "the configuration must be a JSON object";
        return false;
    }
    DaemonConfig c = *out;
    tbs::ReceiverConfig &rc = c.receiver;
    bool ok = true;
    auto section = [&](const Value &s, const char *name) -> const Value & {
        if (!s.isNull() && !s.isObject() && ok) {
            if (error)
                *error = std::string("\"") + name + "\" must be an object";
            ok = false;
        }
        return s;
    };

    const Value &rx = section(v.get("receiver"), "receiver");
    {
        Reader d{section(rx.get("device"), "device"), error};
        d.enumeration("kind", kDevice, rc.device.kind);
        d.num("adapter", rc.device.adapter);
        d.num("frontend", rc.device.frontend);
        d.num("demux", rc.device.demux);
        d.num("bdaIndex", rc.device.bdaIndex);
        d.str("bdaPath", rc.device.bdaDevicePath);
        d.str("replayFile", rc.device.replayFile);
        d.num("replayRateMbps", rc.device.replayRateMbps);
        d.boolean("replayLoop", rc.device.replayLoop);
        ok = ok && d.ok;
    }
    if (ok && rx.has("tuning"))
        ok = tuningFromJson(rx.get("tuning"), &rc.tuning, error);
    if (ok) {
        Reader r{rx, error};
        r.enumeration("capture", kCapture, rc.capture);
        r.num("bufferMB", c.pipelineBufferMB);
        ok = r.ok;
    }
    if (ok) {
        Reader f{section(rx.get("file"), "file"), error};
        f.boolean("enabled", rc.file.enabled);
        f.str("path", rc.file.path);
        f.boolean("timestampSuffix", rc.file.timestampSuffix);
        f.enumeration("bbFormat", kBbFile, rc.file.bbFormat);
        f.num("splitSizeMB", rc.file.splitSizeMB);
        f.num("splitMinutes", rc.file.splitMinutes);
        f.num("keepFiles", rc.file.keepFiles);
        f.num("minFreeMB", rc.file.minFreeMB);
        ok = ok && f.ok;
    }
    if (ok) {
        Reader z{section(rx.get("zmq"), "zmq"), error};
        z.boolean("enabled", rc.zmq.enabled);
        z.str("endpoint", rc.zmq.endpoint);
        z.enumeration("socket", kZmq, rc.zmq.socket);
        z.boolean("bind", rc.zmq.bind);
        z.str("topic", rc.zmq.topic);
        z.boolean("metadataHeader", rc.zmq.metadataHeader);
        z.num("tsPacketsPerMessage", rc.zmq.tsPacketsPerMessage);
        z.num("sendHwm", rc.zmq.sendHwm);
        ok = ok && z.ok;
    }
    if (ok && rx.has("network")) {
        const Value &net = rx.get("network");
        if (!net.isArray()) {
            if (error)
                *error = "\"network\" must be an array of URLs or {\"url\", \"enabled\"} objects";
            return false;
        }
        rc.network.clear();
        for (const Value &n : net.items()) {
            const std::string url = n.isString() ? n.toString() : n.get("url").toString();
            tbs::TsNetworkOutput o;
            std::string err;
            if (!tbs::parseNetworkUrl(url, &o, &err)) {
                if (error)
                    *error = "network output: " + err;
                return false;
            }
            o.enabled = n.isString() ? true : n.get("enabled").toBool(true);
            rc.network.push_back(o);
        }
    }

    const Value &pc = section(v.get("processing"), "processing");
    if (ok) {
        Reader p{pc, error};
        p.boolean("enabled", c.processingEnabled);
        Reader pcap{section(pc.get("pcap"), "pcap"), error};
        pcap.boolean("enabled", c.processing.pcap.enabled);
        pcap.str("path", c.processing.pcap.path);
        pcap.enumeration("format", kPcap, c.processing.pcap.format);
        pcap.boolean("timestampSuffix", c.processing.pcap.timestampSuffix);
        pcap.boolean("ethernetFraming", c.processing.pcap.ethernetFraming);
        pcap.boolean("ipOnly", c.processing.pcap.ipOnly);
        Reader tun{section(pc.get("tun"), "tun"), error};
        tun.boolean("enabled", c.processing.tun.enabled);
        tun.str("name", c.processing.tun.name);
        tun.boolean("tap", c.processing.tun.tap);
        ok = ok && p.ok && pcap.ok && tun.ok;
    }

    if (ok) {
        tbs::MonitorConfig &m = c.monitor;
        Reader r{section(v.get("monitor"), "monitor"), error};
        r.num("historySeconds", m.historySeconds);
        r.boolean("noLock", m.noLock);
        r.num("noLockHoldSec", m.noLockHoldSec);
        r.boolean("lowCnr", m.lowCnr);
        r.num("cnrMinDb", m.cnrMinDb);
        r.num("cnrHoldSec", m.cnrHoldSec);
        r.boolean("lowLevel", m.lowLevel);
        r.num("levelMinDbm", m.levelMinDbm);
        r.num("levelHoldSec", m.levelHoldSec);
        r.boolean("ccErrors", m.ccErrors);
        r.num("ccMaxPerWindow", m.ccMaxPerWindow);
        r.boolean("bbCrcErrors", m.bbCrcErrors);
        r.num("bbCrcMaxPerWindow", m.bbCrcMaxPerWindow);
        r.num("windowSec", m.windowSec);
        r.boolean("lowBitrate", m.lowBitrate);
        r.num("bitrateMinMbps", m.bitrateMinMbps);
        r.num("bitrateHoldSec", m.bitrateHoldSec);
        r.boolean("noData", m.noData);
        r.num("noDataHoldSec", m.noDataHoldSec);
        r.boolean("drops", m.drops);
        r.boolean("outputErrors", m.outputErrors);
        r.str("command", m.command);
        ok = ok && r.ok;
    }
    if (ok) {
        Reader a{section(v.get("api"), "api"), error};
        a.str("listen", c.listen);
        a.str("token", c.token);
        Reader top{v, error};
        top.boolean("autostart", c.autostart);
        top.num("restartDelaySec", c.restartDelaySec);
        ok = ok && a.ok && top.ok;
    }
    if (ok)
        *out = c;
    return ok;
}
