// Python bindings (pybind11) for tbsdvb and tbsbbproc.
//
//   import tbsdvb
//   cfg = tbsdvb.ReceiverConfig()
//   cfg.device.kind = tbsdvb.DeviceKind.FileReplay
//   rx = tbsdvb.Receiver()
//   rx.on_bbframe = lambda frame, header: ...
//   rx.start(cfg)
//
// Callbacks run on the library's threads with the GIL held; keep them short.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <tbsbbproc/CaptureFile.h>
#include <tbsbbproc/NetworkScan.h>
#include <tbsbbproc/Processor.h>
#include <tbsdvb/tbsdvb.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

namespace py = pybind11;
using namespace tbs;

namespace {

py::object optional(bool has, double v)
{
    return has && std::isfinite(v) ? py::object(py::float_(v)) : py::object(py::none());
}

py::bytes bytesOf(const uint8_t *p, size_t n)
{
    return py::bytes(reinterpret_cast<const char *>(p), n);
}

// Calls a Python callback from a library thread; exceptions are reported, not propagated
template <typename... Args>
void callPython(const py::object &fn, Args &&...args)
{
    py::gil_scoped_acquire gil;
    if (!fn || fn.is_none())
        return;
    try {
        fn(std::forward<Args>(args)...);
    } catch (py::error_already_set &e) {
        e.discard_as_unraisable("tbsdvb callback");
    }
}

py::dict eventDict(const bb::EventInfo &e)
{
    py::dict d;
    d["id"] = e.eventId;
    d["start"] = e.startUtc;
    d["duration"] = e.durationSec;
    d["running_status"] = e.runningStatus;
    d["language"] = e.language;
    d["name"] = e.name;
    d["text"] = e.text;
    d["extended_text"] = e.extendedText;
    py::list items;
    for (const auto &it : e.items)
        items.append(py::make_tuple(it.first, it.second));
    d["items"] = items;
    d["genre"] = e.genre;
    d["content"] = e.content;
    d["parental_age"] = e.parentalAge;
    d["scrambled"] = e.scrambled;
    return d;
}

py::dict tsReportDict(const bb::TsReport &t)
{
    py::dict d;
    d["pat_seen"] = t.patSeen;
    d["tsid"] = t.tsid;
    d["onid"] = t.onid;
    d["utc"] = t.hasTime ? py::object(py::int_(t.utcTime)) : py::object(py::none());
    py::list programs;
    for (const auto &p : t.programs) {
        py::dict x;
        x["program_number"] = p.programNumber;
        x["pmt_pid"] = p.pmtPid;
        x["pcr_pid"] = p.pcrPid;
        py::list es;
        for (const auto &e : p.streams) {
            py::dict s;
            s["pid"] = e.pid;
            s["stream_type"] = e.streamType;
            s["kind"] = e.kind;
            s["codec"] = e.codec;
            s["language"] = e.language;
            py::list ca;
            for (const auto &c : e.ca)
                ca.append(py::make_tuple(c.systemName, c.pid));
            s["ca"] = ca;
            es.append(s);
        }
        x["streams"] = es;
        py::list ca;
        for (const auto &c : p.ca)
            ca.append(py::make_tuple(c.systemName, c.pid));
        x["ca"] = ca;
        programs.append(x);
    }
    d["programs"] = programs;
    py::list services;
    for (const auto &s : t.services) {
        py::dict x;
        x["service_id"] = s.serviceId;
        x["tsid"] = s.tsid;
        x["onid"] = s.onid;
        x["actual"] = s.actual;
        x["name"] = s.name;
        x["provider"] = s.provider;
        x["type"] = s.typeName;
        x["scrambled"] = s.scrambled;
        x["now"] = s.present.valid ? py::object(eventDict(s.present)) : py::object(py::none());
        x["next"] = s.following.valid ? py::object(eventDict(s.following)) : py::object(py::none());
        services.append(x);
    }
    d["services"] = services;
    py::list networks;
    for (const auto &n : t.networks) {
        py::dict x;
        x["network_id"] = n.networkId;
        x["actual"] = n.actual;
        x["name"] = n.name;
        py::list tps;
        for (const auto &tp : n.transports) {
            py::dict t(py::arg("tsid") = tp.tsid, py::arg("onid") = tp.onid, py::arg("delivery") = tp.delivery,
                       py::arg("service_ids") = tp.serviceIds);
            if (tp.sat.valid)
                t["satellite"] = py::dict(
                    py::arg("frequency_mhz") = tp.sat.frequencyMHz, py::arg("symbol_rate_ksps") = tp.sat.symbolRateKsps,
                    py::arg("polarization") = std::string(1, "HVLR"[tp.sat.polarization & 3]),
                    py::arg("orbital_position") = tp.sat.orbitalPositionDeg * (tp.sat.east ? 1.0 : -1.0),
                    py::arg("s2") = tp.sat.s2, py::arg("modulation") = tp.sat.modulation, py::arg("fec") = tp.sat.fecInner,
                    py::arg("rolloff") = tp.sat.rolloff, py::arg("isi") = tp.sat.inputStreamId,
                    py::arg("pls_gold") = tp.sat.hasPls ? py::object(py::int_(tp.sat.plsGold)) : py::object(py::none()));
            tps.append(t);
        }
        x["transports"] = tps;
        networks.append(x);
    }
    d["networks"] = networks;
    py::dict usage;
    for (const auto &kv : t.pidUsage)
        usage[py::int_(kv.first)] = kv.second;
    d["pid_usage"] = usage;
    d["sections"] = t.sections;
    d["crc_errors"] = t.crcErrors;
    d["epg_events"] = t.epgEvents;
    d["epg_version"] = t.epgVersion;
    return d;
}

py::dict ipReportDict(const bb::IpReport &ip)
{
    py::dict d;
    d["pdus"] = ip.pdus;
    d["ipv4"] = ip.ipv4;
    d["ipv6"] = ip.ipv6;
    d["non_ip"] = ip.nonIp;
    d["malformed"] = ip.malformed;
    d["ip_bytes"] = ip.ipBytes;
    d["multicast"] = ip.multicast;
    d["mbps"] = ip.mbps;
    d["packets_per_sec"] = ip.packetsPerSec;
    d["flow_count"] = ip.flowCount;
    py::list protos;
    for (const auto &p : ip.protocols)
        protos.append(py::make_tuple(p.name, p.packets, p.bytes));
    d["protocols"] = protos;
    py::list flows;
    for (const auto &f : ip.topFlows) {
        py::dict x;
        x["protocol"] = f.protocolName;
        x["src"] = f.src;
        x["dst"] = f.dst;
        x["src_port"] = f.srcPort;
        x["dst_port"] = f.dstPort;
        x["packets"] = f.packets;
        x["bytes"] = f.bytes;
        x["mbps"] = f.mbps;
        flows.append(x);
    }
    d["flows"] = flows;
    return d;
}

py::dict snapshotDict(const bb::ProcessorSnapshot &s)
{
    py::dict d;
    d["elapsed_sec"] = s.elapsedSec;
    py::list streams;
    for (const auto &st : s.streams) {
        py::dict x;
        x["isi"] = st.isi;
        x["content"] = bb::streamContentName(st.content);
        x["frames"] = st.frames;
        x["data_bytes"] = st.dataBytes;
        x["mbps"] = st.mbps;
        x["header"] = st.hasHeader ? py::object(py::str(st.lastHeader.summary())) : py::object(py::none());
        x["ts_packets"] = st.tsPackets;
        x["ts"] = tsReportDict(st.ts);
        x["gse"] = py::dict(py::arg("data_fields") = st.gse.dataFields, py::arg("packets") = st.gse.packets,
                            py::arg("pdus") = st.gse.pdus, py::arg("crc_errors") = st.gse.crcErrors,
                            py::arg("length_errors") = st.gse.lengthErrors,
                            py::arg("fragment_errors") = st.gse.fragmentErrors);
        x["mpe"] = py::dict(py::arg("pids") = st.mpe.pids, py::arg("sections") = st.mpe.sections,
                            py::arg("datagrams") = st.mpe.datagrams, py::arg("crc_errors") = st.mpe.crcErrors);
        x["ip"] = ipReportDict(st.ip);
        py::list cand;
        for (const auto &c : st.decoderScores)
            cand.append(py::dict(py::arg("decoder") = bb::genericDecoderId(c.decoder), py::arg("coverage") = c.coverage,
                                 py::arg("units") = c.units));
        x["decoder"] = py::dict(py::arg("id") = bb::genericDecoderId(st.decoder),
                                py::arg("name") = bb::genericDecoderName(st.decoder),
                                py::arg("detected") = st.decoderDetected, py::arg("candidates") = cand);
        x["frame_crc32"] = st.frameCrc32;
        x["raw_ip"] = py::dict(py::arg("packets") = st.rawIp.packets, py::arg("bytes") = st.rawIp.bytes,
                               py::arg("skipped_bytes") = st.rawIp.skippedBytes);
        x["ts_in_gs"] = py::dict(py::arg("packets") = st.tsExtract.packets, py::arg("verified") = st.tsExtract.verified,
                                 py::arg("crc_errors") = st.tsExtract.crcErrors,
                                 py::arg("sync_losses") = st.tsExtract.syncLosses);
        streams.append(x);
    }
    d["streams"] = streams;
    d["pcap_file"] = s.pcapFile;
    d["pcap_packets"] = s.pcapPackets;
    d["pcap_error"] = s.pcapError;
    d["tun"] = s.tunName;
    d["tun_packets"] = s.tunPackets;
    d["tun_error"] = s.tunError;
    return d;
}

py::list epgList(const std::vector<bb::ServiceSchedule> &sch)
{
    py::list out;
    for (const auto &s : sch) {
        py::dict x;
        x["service_id"] = s.serviceId;
        x["tsid"] = s.tsid;
        x["onid"] = s.onid;
        x["name"] = s.serviceName;
        x["actual"] = s.actual;
        py::list ev;
        for (const auto &e : s.events)
            ev.append(eventDict(e));
        x["events"] = ev;
        out.append(x);
    }
    return out;
}

// Receiver plus the Python callbacks it calls
class PyReceiver
{
public:
    ~PyReceiver()
    {
        py::gil_scoped_release release; // the library threads may be waiting for the GIL
        rx.stop();
    }

    bool start(const ReceiverConfig &cfg)
    {
        Receiver::Callbacks cb;
        std::shared_ptr<bb::Processor> proc = processor;
        std::shared_ptr<Monitor> mon = monitor;
        // Each callback is installed only when set, so unused data paths cost nothing
        if (onLog)
            cb.onLog = [this](const std::string &m) { callPython(onLog, m); };
        if (onState)
            cb.onStateChanged = [this](ReceiverState s, const std::string &e) { callPython(onState, s, e); };
        if (onStatus)
            cb.onStatus = [this](const FrontendStatus &s) { callPython(onStatus, s); };
        if (onStats || mon)
            cb.onStats = [this, mon](const StreamStats &s) {
                if (mon)
                    mon->update(rx.state(), rx.status(), s);
                if (onStats)
                    callPython(onStats, s);
            };
        // An attached processor is fed in C++, without a Python call per packet
        const bool bbMode = cfg.capture == CaptureMode::BBFrame;
        if (onTs || (proc && !bbMode))
            cb.onTsPackets = [this, proc, bbMode](const uint8_t *p, size_t n) {
                if (proc && !bbMode)
                    proc->pushTs(p, n);
                if (onTs) {
                    py::gil_scoped_acquire gil;
                    callPython(onTs, bytesOf(p, n * 188));
                }
            };
        if (onBbFrame || (proc && bbMode))
            cb.onBbFrame = [this, proc](const uint8_t *f, size_t n, const BbHeader &h) {
                if (proc)
                    proc->pushBbFrame(f, n, h);
                if (onBbFrame) {
                    py::gil_scoped_acquire gil;
                    callPython(onBbFrame, bytesOf(f, n), h);
                }
            };
        if (onPreview)
            cb.onPreviewTs = [this](const uint8_t *p, size_t n) {
                py::gil_scoped_acquire gil;
                callPython(onPreview, bytesOf(p, n));
            };
        rx.setCallbacks(cb);
        py::gil_scoped_release release;
        return rx.start(cfg);
    }

    Receiver rx;
    std::shared_ptr<bb::Processor> processor;
    std::shared_ptr<Monitor> monitor;
    py::object onLog, onState, onStatus, onStats, onTs, onBbFrame, onPreview;
};

class PyProcessor
{
public:
    explicit PyProcessor(const bb::ProcessorConfig &cfg) : ptr(std::make_shared<bb::Processor>(cfg)), proc(*ptr) {}
    std::shared_ptr<bb::Processor> ptr;
    bb::Processor &proc;
};

// Calls a Python progress callback from a library thread (only real progress, not the polls)
Scan::ProgressFn pyProgress(const py::object &fn)
{
    // Ctrl+C cancels: the KeyboardInterrupt stays set and is raised after the scan returns
    return [fn](double f, const std::string &msg) {
        py::gil_scoped_acquire gil;
        if (PyErr_Occurred() || PyErr_CheckSignals() != 0)
            return false;
        if (fn.is_none() || (f < 0 && msg.empty()))
            return true;
        try {
            const py::object r = fn(f, msg);
            return r.is_none() || r.cast<bool>();
        } catch (py::error_already_set &e) {
            e.restore(); // raised after the scan
            return false;
        }
    };
}

py::dict spectrumDict(const SpectrumResult &r)
{
    py::list pts, peaks;
    for (const auto &p : r.points)
        pts.append(py::make_tuple(p.frequencyMHz, p.level));
    for (const auto &p : r.peaks)
        peaks.append(py::dict(py::arg("frequency_mhz") = p.frequencyMHz, py::arg("symbol_rate_ksps") = p.symbolRateKsps,
                              py::arg("level") = p.level, py::arg("snr_db") = p.snrDb));
    return py::dict(py::arg("polarization") = r.polarization, py::arg("decibel") = r.decibel, py::arg("points") = pts,
                    py::arg("peaks") = peaks);
}

py::dict carrierDict(const BlindScanCarrier &c)
{
    return py::dict(py::arg("frequency_mhz") = c.frequencyMHz, py::arg("symbol_rate_ksps") = c.symbolRateKsps,
                    py::arg("delivery_system") = c.deliverySystem,
                    py::arg("modulation") = c.modulation >= 0 ? DvbNames::modulation(c.modulation) : std::string(),
                    py::arg("fec") = c.fec >= 0 ? DvbNames::fec(c.fec) : std::string(), py::arg("isis") = c.isis,
                    py::arg("matype") = c.matype,
                    py::arg("cnr_db") = c.hasCnrDb ? py::object(py::float_(c.cnrDb)) : py::object(py::none()),
                    py::arg("signal_dbm") = c.hasSignalDbm ? py::object(py::float_(c.signalDbm)) : py::object(py::none()),
                    py::arg("tuning") = c.tuning, py::arg("description") = c.describe());
}

} // namespace

PYBIND11_MODULE(tbsdvb, m)
{
    m.doc() = "TBS 6903x DVB-S2/S2X receiver: tuning, TS/BBFrame capture, outputs, and content analysis";
    m.attr("__version__") = tbs::version();

    // ---- Enums ----
    py::enum_<DeviceKind>(m, "DeviceKind")
        .value("LinuxDvb", DeviceKind::LinuxDvb)
        .value("FileReplay", DeviceKind::FileReplay)
        .value("WindowsBda", DeviceKind::WindowsBda);
    py::enum_<DeliverySystem>(m, "DeliverySystem").value("DvbS", DeliverySystem::DvbS).value("DvbS2", DeliverySystem::DvbS2);
    py::enum_<ModulationHint>(m, "ModulationHint")
        .value("Auto", ModulationHint::Auto)
        .value("Qpsk", ModulationHint::Qpsk)
        .value("Psk8", ModulationHint::Psk8)
        .value("Apsk16", ModulationHint::Apsk16)
        .value("Apsk32", ModulationHint::Apsk32);
    py::enum_<LnbType>(m, "LnbType")
        .value("Direct", LnbType::Direct)
        .value("Universal", LnbType::Universal)
        .value("SingleLo", LnbType::SingleLo);
    py::enum_<Polarization>(m, "Polarization")
        .value("Off", Polarization::Off)
        .value("Vertical", Polarization::VerticalRight13V)
        .value("Horizontal", Polarization::HorizontalLeft18V);
    py::enum_<ToneMode>(m, "ToneMode").value("Auto", ToneMode::Auto).value("Off", ToneMode::Off).value("On", ToneMode::On);
    py::enum_<PlsMode>(m, "PlsMode").value("Root", PlsMode::Root).value("Gold", PlsMode::Gold).value("Combo", PlsMode::Combo);
    py::enum_<CaptureMode>(m, "CaptureMode")
        .value("TransportStream", CaptureMode::TransportStream)
        .value("BBFrame", CaptureMode::BBFrame);
    py::enum_<BbFileFormat>(m, "BbFileFormat")
        .value("Raw", BbFileFormat::Raw)
        .value("B8Marker", BbFileFormat::B8Marker)
        .value("PseudoTs", BbFileFormat::PseudoTs);
    py::enum_<ZmqSocketKind>(m, "ZmqSocketKind").value("Pub", ZmqSocketKind::Pub).value("Push", ZmqSocketKind::Push);
    py::enum_<NetProtocol>(m, "NetProtocol").value("Udp", NetProtocol::Udp).value("Rtp", NetProtocol::Rtp).value("Srt", NetProtocol::Srt);
    py::enum_<SrtMode>(m, "SrtMode").value("Caller", SrtMode::Caller).value("Listener", SrtMode::Listener);
    py::enum_<ReceiverState>(m, "ReceiverState")
        .value("Stopped", ReceiverState::Stopped)
        .value("Starting", ReceiverState::Starting)
        .value("Running", ReceiverState::Running)
        .value("Stopping", ReceiverState::Stopping);
    py::enum_<AlarmType> alarmType(m, "AlarmType");
    for (int i = 0; i < static_cast<int>(AlarmType::Count); ++i) {
        const auto t = static_cast<AlarmType>(i);
        std::string name = alarmTypeName(t);
        for (char &c : name)
            c = c == '-' ? '_' : c;
        alarmType.value(name.c_str(), t);
    }
    py::enum_<bb::PcapFormat>(m, "PcapFormat").value("Pcap", bb::PcapFormat::Pcap).value("PcapNg", bb::PcapFormat::PcapNg);

    // ---- Configuration ----
    py::class_<TuningParams>(m, "TuningParams")
        .def(py::init<>())
        .def_readwrite("frequency_mhz", &TuningParams::frequencyMHz)
        .def_readwrite("symbol_rate_ksps", &TuningParams::symbolRateKsps)
        .def_readwrite("delivery_system", &TuningParams::deliverySystem)
        .def_readwrite("modulation", &TuningParams::modulation)
        .def_readwrite("lnb_type", &TuningParams::lnbType)
        .def_readwrite("lnb_lo1_mhz", &TuningParams::lnbLo1MHz)
        .def_readwrite("lnb_lo2_mhz", &TuningParams::lnbLo2MHz)
        .def_readwrite("lnb_switch_mhz", &TuningParams::lnbSwitchMHz)
        .def_readwrite("polarization", &TuningParams::polarization)
        .def_readwrite("tone", &TuningParams::tone)
        .def_readwrite("diseqc_port", &TuningParams::diseqcPort)
        .def_readwrite("input_stream_id", &TuningParams::inputStreamId)
        .def_readwrite("pls_mode", &TuningParams::plsMode)
        .def_readwrite("pls_code", &TuningParams::plsCode)
        .def("intermediate_frequency_khz", &TuningParams::intermediateFrequencyKHz)
        .def("describe", &TuningParams::describe)
        .def("__repr__", [](const TuningParams &t) { return "<TuningParams " + t.describe() + ">"; });

    py::class_<DeviceSelection>(m, "DeviceSelection")
        .def(py::init<>())
        .def_readwrite("kind", &DeviceSelection::kind)
        .def_readwrite("adapter", &DeviceSelection::adapter)
        .def_readwrite("frontend", &DeviceSelection::frontend)
        .def_readwrite("demux", &DeviceSelection::demux)
        .def_readwrite("bda_device_path", &DeviceSelection::bdaDevicePath)
        .def_readwrite("bda_index", &DeviceSelection::bdaIndex)
        .def_readwrite("replay_file", &DeviceSelection::replayFile)
        .def_readwrite("replay_rate_mbps", &DeviceSelection::replayRateMbps)
        .def_readwrite("replay_loop", &DeviceSelection::replayLoop);

    py::class_<FileOutputOptions>(m, "FileOutputOptions")
        .def(py::init<>())
        .def_readwrite("enabled", &FileOutputOptions::enabled)
        .def_readwrite("path", &FileOutputOptions::path)
        .def_readwrite("timestamp_suffix", &FileOutputOptions::timestampSuffix)
        .def_readwrite("bb_format", &FileOutputOptions::bbFormat)
        .def_readwrite("split_size_mb", &FileOutputOptions::splitSizeMB)
        .def_readwrite("split_minutes", &FileOutputOptions::splitMinutes)
        .def_readwrite("keep_files", &FileOutputOptions::keepFiles)
        .def_readwrite("min_free_mb", &FileOutputOptions::minFreeMB);

    py::class_<ZmqOutputOptions>(m, "ZmqOutputOptions")
        .def(py::init<>())
        .def_readwrite("enabled", &ZmqOutputOptions::enabled)
        .def_readwrite("endpoint", &ZmqOutputOptions::endpoint)
        .def_readwrite("socket", &ZmqOutputOptions::socket)
        .def_readwrite("bind", &ZmqOutputOptions::bind)
        .def_readwrite("topic", &ZmqOutputOptions::topic)
        .def_readwrite("metadata_header", &ZmqOutputOptions::metadataHeader)
        .def_readwrite("ts_packets_per_message", &ZmqOutputOptions::tsPacketsPerMessage)
        .def_readwrite("send_hwm", &ZmqOutputOptions::sendHwm);

    py::class_<TsNetworkOutput>(m, "TsNetworkOutput")
        .def(py::init<>())
        .def(py::init([](const std::string &url) {
                 TsNetworkOutput o;
                 std::string err;
                 if (!parseNetworkUrl(url, &o, &err))
                     throw py::value_error(err);
                 return o;
             }),
             py::arg("url"), "From a URL such as udp://239.1.1.1:1234?program=101")
        .def_readwrite("enabled", &TsNetworkOutput::enabled)
        .def_readwrite("protocol", &TsNetworkOutput::protocol)
        .def_readwrite("host", &TsNetworkOutput::host)
        .def_readwrite("port", &TsNetworkOutput::port)
        .def_readwrite("interface_name", &TsNetworkOutput::interfaceName)
        .def_readwrite("ttl", &TsNetworkOutput::ttl)
        .def_readwrite("packets_per_datagram", &TsNetworkOutput::packetsPerDatagram)
        .def_readwrite("isi", &TsNetworkOutput::isi)
        .def_readwrite("program_number", &TsNetworkOutput::programNumber)
        .def_readwrite("pids", &TsNetworkOutput::pids)
        .def_readwrite("drop_null_packets", &TsNetworkOutput::dropNullPackets)
        .def_readwrite("srt_mode", &TsNetworkOutput::srtMode)
        .def_readwrite("srt_latency_ms", &TsNetworkOutput::srtLatencyMs)
        .def_readwrite("srt_passphrase", &TsNetworkOutput::srtPassphrase)
        .def_readwrite("srt_stream_id", &TsNetworkOutput::srtStreamId)
        .def("url", [](const TsNetworkOutput &o, bool redact) { return networkUrl(o, redact); }, py::arg("redact") = false)
        .def("__repr__", [](const TsNetworkOutput &o) { return "<TsNetworkOutput " + networkUrl(o, true) + ">"; });

    py::class_<ReceiverConfig>(m, "ReceiverConfig")
        .def(py::init<>())
        .def_readwrite("device", &ReceiverConfig::device)
        .def_readwrite("tuning", &ReceiverConfig::tuning)
        .def_readwrite("capture", &ReceiverConfig::capture)
        .def_readwrite("file", &ReceiverConfig::file)
        .def_readwrite("zmq", &ReceiverConfig::zmq)
        .def_readwrite("network", &ReceiverConfig::network);

    // ---- Status ----
    py::class_<BbHeader>(m, "BbHeader")
        .def(py::init<>())
        .def_static("parse", [](py::bytes data) {
            const std::string s = data;
            if (s.size() < BbHeader::kSize)
                throw py::value_error("need 10 bytes");
            BbHeader h;
            if (!h.parse(reinterpret_cast<const uint8_t *>(s.data())))
                throw py::value_error("BBHeader CRC-8 does not match");
            return h;
        })
        .def_readonly("matype1", &BbHeader::matype1)
        .def_readonly("matype2", &BbHeader::matype2)
        .def_readonly("upl", &BbHeader::upl)
        .def_readonly("dfl", &BbHeader::dfl)
        .def_readonly("sync", &BbHeader::sync)
        .def_readonly("syncd", &BbHeader::syncd)
        .def_property_readonly("isi", &BbHeader::isi)
        .def_property_readonly("stream_type", &BbHeader::streamTypeName)
        .def_property_readonly("mode", &BbHeader::modeName)
        .def_property_readonly("rolloff", &BbHeader::rolloffName)
        .def_property_readonly("single_input_stream", &BbHeader::singleInputStream)
        .def_property_readonly("ccm", &BbHeader::ccm)
        .def_property_readonly("data_field_bytes", &BbHeader::dataFieldBytes)
        .def("summary", &BbHeader::summary)
        .def("__repr__", [](const BbHeader &h) { return "<BbHeader " + h.summary() + ">"; });

    py::class_<FrontendStatus>(m, "FrontendStatus")
        .def_readonly("valid", &FrontendStatus::valid)
        .def_readonly("flags", &FrontendStatus::flags)
        .def_property_readonly("locked", &FrontendStatus::locked)
        .def_property_readonly("flags_text", [](const FrontendStatus &s) { return DvbNames::statusFlags(s.flags); })
        .def_property_readonly("level_dbm", [](const FrontendStatus &s) { return optional(s.hasSignalDbm, s.signalDbm); })
        .def_property_readonly("level_percent", [](const FrontendStatus &s) { return optional(s.hasSignalPercent, s.signalPercent); })
        .def_property_readonly("cnr_db", [](const FrontendStatus &s) { return optional(s.hasCnrDb, s.cnrDb); })
        .def_property_readonly("cnr_percent", [](const FrontendStatus &s) { return optional(s.hasCnrPercent, s.cnrPercent); })
        .def_property_readonly("ber", [](const FrontendStatus &s) { return optional(s.hasBer, s.ber); })
        .def_property_readonly("error_blocks", [](const FrontendStatus &s) {
            return s.hasErrorBlocks ? py::object(py::int_(s.errorBlocks)) : py::object(py::none());
        })
        .def_property_readonly("temperature_c", [](const FrontendStatus &s) { return optional(s.hasTemperature, s.temperatureC); })
        .def_property_readonly("delivery_system", [](const FrontendStatus &s) { return DvbNames::deliverySystem(s.deliverySystem); })
        .def_property_readonly("modulation", [](const FrontendStatus &s) { return DvbNames::modulation(s.modulation); })
        .def_property_readonly("fec", [](const FrontendStatus &s) { return DvbNames::fec(s.fec); })
        .def_property_readonly("rolloff", [](const FrontendStatus &s) { return DvbNames::rolloff(s.rolloff); })
        .def_property_readonly("pilot", [](const FrontendStatus &s) { return DvbNames::pilot(s.pilot); })
        .def_readonly("frequency_khz", &FrontendStatus::frequencyKHz)
        .def_readonly("symbol_rate", &FrontendStatus::symbolRate)
        .def_readonly("stream_id", &FrontendStatus::streamId)
        .def_readonly("device_name", &FrontendStatus::deviceName);

    py::class_<NetOutputStatus>(m, "NetOutputStatus")
        .def_readonly("url", &NetOutputStatus::url)
        .def_readonly("state", &NetOutputStatus::state)
        .def_readonly("active", &NetOutputStatus::active)
        .def_readonly("peers", &NetOutputStatus::peers)
        .def_readonly("packets", &NetOutputStatus::packets)
        .def_readonly("bytes", &NetOutputStatus::bytes)
        .def_readonly("send_errors", &NetOutputStatus::sendErrors)
        .def_readonly("mbps", &NetOutputStatus::mbps);

    py::class_<StreamStats>(m, "StreamStats")
        .def_readonly("elapsed_sec", &StreamStats::elapsedSec)
        .def_readonly("bytes_in", &StreamStats::bytesIn)
        .def_readonly("input_mbps", &StreamStats::inputMbps)
        .def_readonly("driver_overflows", &StreamStats::driverOverflows)
        .def_readonly("pipeline_dropped_bytes", &StreamStats::pipelineDroppedBytes)
        .def_readonly("ts_packets", &StreamStats::tsPackets)
        .def_readonly("sync_losses", &StreamStats::syncLosses)
        .def_readonly("cc_errors", &StreamStats::ccErrors)
        .def_readonly("tei_errors", &StreamStats::teiErrors)
        .def_readonly("pid_counts", &StreamStats::pidCounts)
        .def_readonly("bb_frames", &StreamStats::bbFrames)
        .def_readonly("bb_frames_per_sec", &StreamStats::bbFramesPerSec)
        .def_readonly("bb_payload_mbps", &StreamStats::bbPayloadMbps)
        .def_readonly("bb_crc_errors", &StreamStats::bbCrcErrors)
        .def_property_readonly("bb_header", [](const StreamStats &s) {
            return s.hasBbHeader ? py::cast(s.lastBbHeader) : py::object(py::none());
        })
        .def_readonly("file_name", &StreamStats::fileName)
        .def_readonly("file_bytes", &StreamStats::fileBytes)
        .def_readonly("file_stopped", &StreamStats::fileStopped)
        .def_readonly("zmq_messages", &StreamStats::zmqMessages)
        .def_readonly("zmq_dropped", &StreamStats::zmqDropped)
        .def_readonly("network", &StreamStats::network);

    py::class_<DeviceInfo>(m, "DeviceInfo")
        .def_readonly("kind", &DeviceInfo::kind)
        .def_readonly("name", &DeviceInfo::name)
        .def_readonly("path", &DeviceInfo::path)
        .def_readonly("adapter", &DeviceInfo::adapter)
        .def_readonly("frontend", &DeviceInfo::frontend)
        .def_readonly("index", &DeviceInfo::index)
        .def("label", &DeviceInfo::label)
        .def("apply_to", [](const DeviceInfo &d, DeviceSelection &sel) { d.applyTo(sel); })
        .def("__repr__", [](const DeviceInfo &d) { return "<DeviceInfo " + d.label() + ">"; });

    m.def("enumerate_devices", &Devices::enumerate, py::arg("include_replay") = true, "Receivers on this machine");
    m.def(
        "diagnose",
        [](const DeviceSelection &sel) {
            py::list out;
            for (const DiagnosticItem &i : Devices::diagnose(sel)) {
                static const char *const levels[] = {"ok", "info", "warning", "error"};
                out.append(py::make_tuple(levels[static_cast<int>(i.level)], i.message, i.hint));
            }
            return out;
        },
        "Driver / device checks as (level, message, hint) tuples");

    // ---- Receiver ----
    py::class_<PyReceiver>(m, "Receiver", "Tunes the card and captures TS or BBFrames on background threads")
        .def(py::init<>())
        .def("start", &PyReceiver::start, py::arg("config"),
             "Starts in the background; errors arrive through on_state / last_error()")
        .def("stop", [](PyReceiver &r) {
            py::gil_scoped_release release;
            r.rx.stop();
        })
        .def("retune", [](PyReceiver &r, const TuningParams &t) {
            py::gil_scoped_release release;
            r.rx.retune(t);
        })
        .def("state", [](PyReceiver &r) { return r.rx.state(); })
        .def("is_running", [](PyReceiver &r) { return r.rx.isRunning(); })
        .def("last_error", [](PyReceiver &r) { return r.rx.lastError(); })
        .def("status", [](PyReceiver &r) { return r.rx.status(); })
        .def("stats", [](PyReceiver &r) { return r.rx.stats(); })
        .def("set_status_interval", [](PyReceiver &r, int ms) { r.rx.setStatusInterval(ms); })
        .def("set_preview_enabled", [](PyReceiver &r, bool on) { r.rx.setPreviewEnabled(on); })
        .def("set_preview_stream", [](PyReceiver &r, int isi) { r.rx.setPreviewStream(isi); })
        .def("set_constellation_samples", [](PyReceiver &r, int n) { r.rx.setConstellationSamples(n); },
             "Read N IQ samples with each status update (0 = off); needs the neumoDVB driver")
        .def("constellation", [](PyReceiver &r) -> py::object {
            bool supported = true;
            const auto iq = r.rx.constellation(&supported);
            if (!supported)
                return py::none();
            py::list out;
            for (const auto &s : iq)
                out.append(py::make_tuple(static_cast<int>(s.i), static_cast<int>(s.q)));
            return out;
        }, "Latest IQ samples as (i, q) tuples, None if the device cannot deliver them")
        .def("attach_processor", [](PyReceiver &r, PyProcessor *p) { r.processor = p ? p->ptr : nullptr; },
             py::arg("processor"), "Feed this Processor from the capture threads (set before start)")
        .def("attach_monitor", [](PyReceiver &r, std::shared_ptr<Monitor> m) { r.monitor = std::move(m); },
             py::arg("monitor"), "Feed this Monitor with every stats interval (set before start)")
        .def_readwrite("on_log", &PyReceiver::onLog, "f(message: str)")
        .def_readwrite("on_state", &PyReceiver::onState, "f(state: ReceiverState, error: str)")
        .def_readwrite("on_status", &PyReceiver::onStatus, "f(status: FrontendStatus)")
        .def_readwrite("on_stats", &PyReceiver::onStats, "f(stats: StreamStats)")
        .def_readwrite("on_ts", &PyReceiver::onTs, "f(packets: bytes) - N x 188-byte TS packets")
        .def_readwrite("on_bbframe", &PyReceiver::onBbFrame, "f(frame: bytes, header: BbHeader) - BBHeader + data field")
        .def_readwrite("on_preview", &PyReceiver::onPreview, "f(ts: bytes) - decodable TS while the preview is enabled");

    // ---- Content processing (tbsbbproc) ----
    py::class_<bb::PcapOptions>(m, "PcapOptions")
        .def(py::init<>())
        .def_readwrite("enabled", &bb::PcapOptions::enabled)
        .def_readwrite("path", &bb::PcapOptions::path)
        .def_readwrite("format", &bb::PcapOptions::format)
        .def_readwrite("timestamp_suffix", &bb::PcapOptions::timestampSuffix)
        .def_readwrite("ethernet_framing", &bb::PcapOptions::ethernetFraming)
        .def_readwrite("ip_only", &bb::PcapOptions::ipOnly);
    py::class_<bb::TunOptions>(m, "TunOptions")
        .def(py::init<>())
        .def_readwrite("enabled", &bb::TunOptions::enabled)
        .def_readwrite("name", &bb::TunOptions::name)
        .def_readwrite("tap", &bb::TunOptions::tap);
    py::class_<bb::ProcessorConfig>(m, "ProcessorConfig")
        .def(py::init<>())
        .def_readwrite("analyze_tables", &bb::ProcessorConfig::analyzeTables)
        .def_readwrite("decode_gse", &bb::ProcessorConfig::decodeGse)
        .def_readwrite("decode_mpe", &bb::ProcessorConfig::decodeMpe)
        .def_readwrite("pcap", &bb::ProcessorConfig::pcap)
        .def_readwrite("tun", &bb::ProcessorConfig::tun)
        .def_readwrite("top_flows", &bb::ProcessorConfig::topFlows)
        .def_readwrite("probe_frames", &bb::ProcessorConfig::probeFrames)
        .def_readwrite("strip_frame_crc32", &bb::ProcessorConfig::stripFrameCrc32)
        .def_property(
            "generic_decoder", [](const bb::ProcessorConfig &c) { return std::string(bb::genericDecoderId(c.genericDecoder)); },
            [](bb::ProcessorConfig &c, const std::string &id) {
                if (!bb::parseGenericDecoder(id, &c.genericDecoder))
                    throw std::invalid_argument("unknown decoder '" + id
                                                + "' (auto, gse, gse-len2, gse-split, gse-len2-split, ts, crc-ts, ip, ip-swapped, none)");
            },
            "Generic stream decoder: 'auto' (default) detects standard GSE or a non-standard variant");

    py::class_<PyProcessor>(m, "Processor",
                            "Per-ISI content analysis: PSI/SI tables and EPG of TS streams, GSE/MPE IP extraction")
        .def(py::init<const bb::ProcessorConfig &>(), py::arg("config") = bb::ProcessorConfig())
        .def("open", [](PyProcessor &p) {
            std::string err;
            if (!p.proc.open(&err))
                throw std::runtime_error(err);
        })
        .def("close", [](PyProcessor &p) { p.proc.close(); })
        .def("finish", [](PyProcessor &p) { p.proc.finish(); },
             "End of input: decide generic stream decoders still being auto-detected")
        .def("push_bbframe", [](PyProcessor &p, py::bytes frame, const BbHeader &h) {
            const std::string s = frame;
            p.proc.pushBbFrame(reinterpret_cast<const uint8_t *>(s.data()), s.size(), h);
        })
        .def("push_ts", [](PyProcessor &p, py::bytes packets) {
            const std::string s = packets;
            p.proc.pushTs(reinterpret_cast<const uint8_t *>(s.data()), s.size() / 188);
        })
        .def("snapshot", [](PyProcessor &p) { return snapshotDict(p.proc.snapshot()); }, "Report as nested dicts")
        .def("report", [](PyProcessor &p, bool verbose) { return bb::formatReport(p.proc.snapshot(), verbose); },
             py::arg("verbose") = false, "Human readable report")
        .def("epg", [](PyProcessor &p, int isi) { return epgList(p.proc.epg(isi)); }, py::arg("isi") = -1)
        .def("xmltv", [](PyProcessor &p) {
            std::vector<bb::ServiceSchedule> all;
            for (const auto &st : p.proc.snapshot().streams)
                for (auto &s : p.proc.epg(st.isi))
                    all.push_back(std::move(s));
            return bb::formatXmltv(all);
        })
        .def("reset", [](PyProcessor &p) { p.proc.reset(); })
        ;

    m.def(
        "read_capture",
        [](const std::string &path, const py::object &onBbFrame, const py::object &onTs) {
            bb::CaptureCallbacks cb;
            if (!onBbFrame.is_none())
                cb.onBbFrame = [&](const uint8_t *f, size_t n, const BbHeader &h) { onBbFrame(bytesOf(f, n), h); };
            if (!onTs.is_none())
                cb.onTs = [&](const uint8_t *p, size_t n) { onTs(bytesOf(p, n * 188)); };
            bb::CaptureFormat fmt = bb::CaptureFormat::Unknown;
            std::string err;
            if (!bb::readCaptureFile(path, cb, &fmt, &err))
                throw std::runtime_error(err);
            return std::string(bb::captureFormatName(fmt));
        },
        py::arg("path"), py::arg("on_bbframe") = py::none(), py::arg("on_ts") = py::none(),
        "Reads TS, pseudo-TS, raw or 0xB8 BBFrame files; returns the detected format");
    m.def(
        "analyze_capture",
        [](const std::string &path, const bb::ProcessorConfig &cfg) {
            // Whole-file analysis in C++ (fast): returns (format, snapshot dict, xmltv)
            bb::Processor proc(cfg);
            std::string err;
            if (!proc.open(&err))
                throw std::runtime_error(err);
            bb::CaptureCallbacks cb;
            cb.onBbFrame = [&](const uint8_t *f, size_t n, const BbHeader &h) { proc.pushBbFrame(f, n, h); };
            cb.onTs = [&](const uint8_t *p, size_t n) { proc.pushTs(p, n); };
            bb::CaptureFormat fmt = bb::CaptureFormat::Unknown;
            {
                py::gil_scoped_release release;
                if (!bb::readCaptureFile(path, cb, &fmt, &err)) {
                    py::gil_scoped_acquire gil;
                    throw std::runtime_error(err);
                }
                proc.close();
            }
            const bb::ProcessorSnapshot snap = proc.snapshot();
            std::vector<bb::ServiceSchedule> all;
            for (const auto &st : snap.streams)
                for (auto &s : proc.epg(st.isi))
                    all.push_back(std::move(s));
            return py::make_tuple(std::string(bb::captureFormatName(fmt)), snapshotDict(snap), bb::formatXmltv(all));
        },
        py::arg("path"), py::arg("config") = bb::ProcessorConfig());

    // ---- Spectrum, blind scan, network scan ----
    py::enum_<SpectrumMethod>(m, "SpectrumMethod").value("Sweep", SpectrumMethod::Sweep).value("Fft", SpectrumMethod::Fft);
    m.def(
        "scan_capabilities",
        [](const DeviceSelection &dev) {
            ScanCapabilities c;
            {
                py::gil_scoped_release release;
                c = Scan::capabilities(dev);
            }
            return py::dict(py::arg("probed") = c.probed, py::arg("neumo_api") = c.neumoApi,
                            py::arg("spectrum_sweep") = c.spectrumSweep, py::arg("spectrum_fft") = c.spectrumFft,
                            py::arg("blind_scan") = c.blindScan, py::arg("constellation") = c.constellation,
                            py::arg("note") = c.note);
        },
        py::arg("device"), "What the driver supports (spectrum, blind scan, IQ); opens the frontend briefly");
    m.def(
        "spectrum",
        [](const DeviceSelection &dev, double start, double end, const TuningParams &lnb, SpectrumMethod method,
           int resolution, int fft, const py::object &progress) {
            SpectrumRequest req;
            req.startMHz = start;
            req.endMHz = end;
            req.lnb = lnb;
            req.method = method;
            req.resolutionKHz = resolution;
            req.fftSize = fft;
            SpectrumResult res;
            std::string err;
            bool ok;
            {
                py::gil_scoped_release release;
                ok = Scan::spectrum(dev, req, &res, &err, pyProgress(progress));
            }
            if (PyErr_Occurred())
                throw py::error_already_set();
            if (!ok)
                throw std::runtime_error(err);
            return spectrumDict(res);
        },
        py::arg("device"), py::arg("start_mhz"), py::arg("end_mhz"), py::arg("lnb") = TuningParams(),
        py::arg("method") = SpectrumMethod::Fft, py::arg("resolution_khz") = 0, py::arg("fft_size") = 512,
        py::arg("progress") = py::none(),
        "RF spectrum (neumoDVB driver). lnb gives LNB type, polarization and DiSEqC; progress(fraction, message) -> bool");
    m.def(
        "blind_scan",
        [](const DeviceSelection &dev, double start, double end, const TuningParams &lnb, int maxSr,
           const py::object &progress, const py::object &onCarrier) {
            BlindScanRequest req;
            req.startMHz = start;
            req.endMHz = end;
            req.lnb = lnb;
            req.maxSymbolRateKsps = maxSr;
            std::vector<BlindScanCarrier> found;
            std::string err;
            bool ok;
            {
                py::gil_scoped_release release;
                ok = Scan::blindScan(dev, req, &found, &err, pyProgress(progress), [&](const BlindScanCarrier &c) {
                    if (onCarrier.is_none())
                        return;
                    py::gil_scoped_acquire gil;
                    if (PyErr_Occurred())
                        return;
                    try {
                        onCarrier(carrierDict(c));
                    } catch (py::error_already_set &e) {
                        e.restore();
                    }
                });
            }
            if (PyErr_Occurred())
                throw py::error_already_set();
            if (!ok)
                throw std::runtime_error(err);
            py::list out;
            for (const auto &c : found)
                out.append(carrierDict(c));
            return out;
        },
        py::arg("device"), py::arg("start_mhz"), py::arg("end_mhz"), py::arg("lnb") = TuningParams(),
        py::arg("max_symbol_rate_ksps") = 45000, py::arg("progress") = py::none(), py::arg("on_carrier") = py::none(),
        "Finds and locks the carriers in a range (neumoDVB driver); returns a list of dicts with a ready 'tuning'");
    m.def(
        "network_scan",
        [](PyReceiver &rx, PyProcessor &proc, const std::vector<TuningParams> &seeds, bool followNit, bool nitOther,
           bool allPositions, int lockTimeoutMs, int tableTimeoutMs, const py::object &progress) {
            bb::NetworkScanOptions opt;
            opt.followNit = followNit;
            opt.includeNitOther = nitOther;
            opt.sameOrbitalPosition = !allPositions;
            opt.lockTimeoutMs = lockTimeoutMs;
            opt.tableTimeoutMs = tableTimeoutMs;
            if (rx.processor != proc.ptr)
                throw std::runtime_error("attach the processor to the receiver (attach_processor) before starting it");
            bb::NetworkScanResult r;
            {
                py::gil_scoped_release release;
                std::atomic<bool> cancel{false};
                r = bb::runNetworkScan(
                    rx.rx, proc.proc, seeds, opt,
                    [&](const bb::NetworkScanProgress &p) {
                        py::gil_scoped_acquire gil;
                        if (PyErr_Occurred() || PyErr_CheckSignals() != 0) {
                            cancel = true;
                            return;
                        }
                        if (progress.is_none())
                            return;
                        try {
                            progress(p.index, p.total, p.message);
                        } catch (py::error_already_set &e) {
                            e.restore();
                            cancel = true;
                        }
                    },
                    {}, &cancel);
            }
            if (PyErr_Occurred())
                throw py::error_already_set();
            py::list tps;
            for (const auto &t : r.transponders) {
                py::list services;
                for (const auto &sv : t.services)
                    services.append(py::dict(py::arg("service_id") = sv.serviceId, py::arg("name") = sv.name,
                                             py::arg("provider") = sv.provider, py::arg("type") = sv.typeName,
                                             py::arg("scrambled") = sv.scrambled));
                tps.append(py::dict(py::arg("tuning") = t.tuning, py::arg("locked") = t.locked,
                                    py::arg("tables_complete") = t.tablesComplete, py::arg("tsid") = t.tsid,
                                    py::arg("onid") = t.onid, py::arg("delivery") = t.delivery,
                                    py::arg("services") = services));
            }
            return py::dict(py::arg("network_id") = r.networkId, py::arg("network_name") = r.networkName,
                            py::arg("transponders") = tps, py::arg("text") = bb::formatNetworkScan(r));
        },
        py::arg("receiver"), py::arg("processor"), py::arg("seeds"), py::arg("follow_nit") = true,
        py::arg("nit_other") = false, py::arg("all_positions") = false, py::arg("lock_timeout_ms") = 4000,
        py::arg("table_timeout_ms") = 12000, py::arg("progress") = py::none(),
        "Tunes the running receiver to each seed and every transport its NIT lists; collects the services");

    // ---- TS filter ----
    py::class_<TsFilter>(m, "TsFilter", "Whole TS (nulls dropped), a PID list, or one service as an SPTS")
        .def(py::init([](int program, std::vector<uint16_t> pids, bool dropNull) {
                 return TsFilter(TsFilterOptions{program, std::move(pids), dropNull});
             }),
             py::arg("program") = 0, py::arg("pids") = std::vector<uint16_t>(), py::arg("drop_null_packets") = true)
        .def("push", [](TsFilter &f, py::bytes packets) {
            const std::string s = packets;
            std::string out;
            for (size_t i = 0; i + 188 <= s.size(); i += 188)
                f.push(reinterpret_cast<const uint8_t *>(s.data() + i),
                       [&](const uint8_t *p) { out.append(reinterpret_cast<const char *>(p), 188); });
            return py::bytes(out);
        })
        .def("reset", &TsFilter::reset)
        .def_property_readonly("program_found", &TsFilter::programFound)
        .def_property_readonly("pmt_pid", &TsFilter::pmtPid)
        .def_property_readonly("service_pids", &TsFilter::servicePids);

    // ---- Monitoring ----
    py::class_<MonitorConfig>(m, "MonitorConfig")
        .def(py::init<>())
        .def_readwrite("history_seconds", &MonitorConfig::historySeconds)
        .def_readwrite("no_lock", &MonitorConfig::noLock)
        .def_readwrite("no_lock_hold_sec", &MonitorConfig::noLockHoldSec)
        .def_readwrite("low_cnr", &MonitorConfig::lowCnr)
        .def_readwrite("cnr_min_db", &MonitorConfig::cnrMinDb)
        .def_readwrite("cnr_hold_sec", &MonitorConfig::cnrHoldSec)
        .def_readwrite("low_level", &MonitorConfig::lowLevel)
        .def_readwrite("level_min_dbm", &MonitorConfig::levelMinDbm)
        .def_readwrite("level_hold_sec", &MonitorConfig::levelHoldSec)
        .def_readwrite("cc_errors", &MonitorConfig::ccErrors)
        .def_readwrite("cc_max_per_window", &MonitorConfig::ccMaxPerWindow)
        .def_readwrite("bb_crc_errors", &MonitorConfig::bbCrcErrors)
        .def_readwrite("bb_crc_max_per_window", &MonitorConfig::bbCrcMaxPerWindow)
        .def_readwrite("window_sec", &MonitorConfig::windowSec)
        .def_readwrite("low_bitrate", &MonitorConfig::lowBitrate)
        .def_readwrite("bitrate_min_mbps", &MonitorConfig::bitrateMinMbps)
        .def_readwrite("bitrate_hold_sec", &MonitorConfig::bitrateHoldSec)
        .def_readwrite("no_data", &MonitorConfig::noData)
        .def_readwrite("no_data_hold_sec", &MonitorConfig::noDataHoldSec)
        .def_readwrite("drops", &MonitorConfig::drops)
        .def_readwrite("output_errors", &MonitorConfig::outputErrors)
        .def_readwrite("command", &MonitorConfig::command);
    py::class_<Alarm>(m, "Alarm")
        .def_readonly("type", &Alarm::type)
        .def_readonly("enabled", &Alarm::enabled)
        .def_readonly("active", &Alarm::active)
        .def_readonly("since_ms", &Alarm::sinceMs)
        .def_readonly("raised_count", &Alarm::raisedCount)
        .def_readonly("message", &Alarm::message)
        .def_property_readonly("title", [](const Alarm &a) { return alarmTypeTitle(a.type); });
    py::class_<AlarmEvent>(m, "AlarmEvent")
        .def_readonly("time_ms", &AlarmEvent::timeMs)
        .def_readonly("type", &AlarmEvent::type)
        .def_readonly("raised", &AlarmEvent::raised)
        .def_readonly("message", &AlarmEvent::message);
    py::class_<MonitorSample>(m, "MonitorSample")
        .def_readonly("time_ms", &MonitorSample::timeMs)
        .def_readonly("running", &MonitorSample::running)
        .def_readonly("locked", &MonitorSample::locked)
        .def_readonly("level_dbm", &MonitorSample::levelDbm)
        .def_readonly("cnr_db", &MonitorSample::cnrDb)
        .def_readonly("ber", &MonitorSample::ber)
        .def_readonly("input_mbps", &MonitorSample::inputMbps)
        .def_readonly("payload_mbps", &MonitorSample::payloadMbps)
        .def_readonly("cc_errors", &MonitorSample::ccErrors)
        .def_readonly("bb_crc_errors", &MonitorSample::bbCrcErrors);
    py::class_<Monitor, std::shared_ptr<Monitor>>(m, "Monitor", "Receiver history and alarms (Receiver.attach_monitor)")
        .def(py::init([](const MonitorConfig &c) { return std::make_shared<Monitor>(c); }), py::arg("config") = MonitorConfig())
        .def("set_config", &Monitor::setConfig)
        .def("set_alarm_callback", [](Monitor &mon, py::object fn) {
            if (fn.is_none()) {
                mon.setAlarmCallback(nullptr);
                return;
            }
            // Holds the Python callable; released with the GIL whichever thread drops it last
            auto holder = std::shared_ptr<py::object>(new py::object(std::move(fn)), [](py::object *o) {
                py::gil_scoped_acquire gil;
                delete o;
            });
            mon.setAlarmCallback([holder](const AlarmEvent &e) { callPython(*holder, e); });
        }, py::arg("callback"), "f(event: AlarmEvent) on every raised or cleared alarm")
        .def("config", &Monitor::config)
        .def("update", &Monitor::update, py::arg("state"), py::arg("status"), py::arg("stats"), py::arg("now_ms") = 0)
        .def("reset", &Monitor::reset)
        .def("history", &Monitor::history, py::arg("since_ms") = 0)
        .def("alarms", &Monitor::alarms)
        .def("events", &Monitor::events, py::arg("max") = 500)
        .def("active_alarms", &Monitor::activeAlarms)
        .def("write_csv", [](const Monitor &mon, const std::string &path, int64_t since) {
            std::string err;
            if (!mon.writeCsv(path, since, &err))
                throw std::runtime_error(err);
        }, py::arg("path"), py::arg("since_ms") = 0);

    // ---- Helpers ----
    m.def("parse_network_url", [](const std::string &url) {
        TsNetworkOutput o;
        std::string err;
        if (!parseNetworkUrl(url, &o, &err))
            throw py::value_error(err);
        return o;
    });
    m.def("crc32_mpeg", [](py::bytes data) {
        const std::string s = data;
        return crc32Mpeg(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    });
}
