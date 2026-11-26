#include "SettingsStore.h"

#include <QSettings>

namespace SettingsStore {
namespace {

template <typename E>
E readEnum(QSettings &s, const char *key, E def)
{
    return static_cast<E>(s.value(QLatin1String(key), static_cast<int>(def)).toInt());
}
template <typename E>
void writeEnum(QSettings &s, const char *key, E v)
{
    s.setValue(QLatin1String(key), static_cast<int>(v));
}
std::string readString(QSettings &s, const char *key, const std::string &def)
{
    return s.value(QLatin1String(key), QString::fromStdString(def)).toString().toStdString();
}
void writeString(QSettings &s, const char *key, const std::string &v)
{
    s.setValue(QLatin1String(key), QString::fromStdString(v));
}
template <typename T>
T read(QSettings &s, const char *key, T def)
{
    return s.value(QLatin1String(key), QVariant::fromValue(def)).template value<T>();
}

} // namespace

tbs::ReceiverConfig load(QSettings &s)
{
    const tbs::ReceiverConfig d;
    tbs::ReceiverConfig c;

    s.beginGroup(QStringLiteral("tuner"));
    c.device.kind = readEnum(s, "deviceKind", d.device.kind);
    c.device.adapter = read(s, "adapter", d.device.adapter);
    c.device.frontend = read(s, "frontend", d.device.frontend);
    c.device.demux = read(s, "demux", d.device.demux);
    c.device.bdaIndex = read(s, "bdaIndex", d.device.bdaIndex);
    c.device.bdaDevicePath = readString(s, "bdaDevicePath", d.device.bdaDevicePath);
    c.device.replayFile = readString(s, "replayFile", d.device.replayFile);
    c.device.replayRateMbps = read(s, "replayRateMbps", d.device.replayRateMbps);
    c.device.replayLoop = read(s, "replayLoop", d.device.replayLoop);

    tbs::TuningParams &t = c.tuning;
    t.frequencyMHz = read(s, "frequencyMHz", d.tuning.frequencyMHz);
    t.symbolRateKsps = read(s, "symbolRateKsps", d.tuning.symbolRateKsps);
    t.deliverySystem = readEnum(s, "deliverySystem", d.tuning.deliverySystem);
    t.modulation = readEnum(s, "modulation", d.tuning.modulation);
    t.lnbType = readEnum(s, "lnbType", d.tuning.lnbType);
    t.lnbLo1MHz = read(s, "lnbLo1MHz", d.tuning.lnbLo1MHz);
    t.lnbLo2MHz = read(s, "lnbLo2MHz", d.tuning.lnbLo2MHz);
    t.lnbSwitchMHz = read(s, "lnbSwitchMHz", d.tuning.lnbSwitchMHz);
    t.polarization = readEnum(s, "polarization", d.tuning.polarization);
    t.tone = readEnum(s, "tone", d.tuning.tone);
    t.diseqcPort = read(s, "diseqcPort", d.tuning.diseqcPort);
    t.inputStreamId = read(s, "inputStreamId", d.tuning.inputStreamId);
    t.plsMode = readEnum(s, "plsMode", d.tuning.plsMode);
    t.plsCode = read(s, "plsCode", d.tuning.plsCode);
    c.capture = readEnum(s, "captureMode", d.capture);
    s.endGroup();

    s.beginGroup(QStringLiteral("output"));
    c.file.enabled = read(s, "fileEnabled", d.file.enabled);
    c.file.path = readString(s, "filePath", d.file.path);
    c.file.timestampSuffix = read(s, "fileTimestampSuffix", d.file.timestampSuffix);
    c.file.bbFormat = readEnum(s, "bbFileFormat", d.file.bbFormat);
    c.file.splitSizeMB = read(s, "fileSplitSizeMB", static_cast<qulonglong>(d.file.splitSizeMB));
    c.file.splitMinutes = read(s, "fileSplitMinutes", d.file.splitMinutes);
    c.file.keepFiles = read(s, "fileKeepFiles", d.file.keepFiles);
    c.file.minFreeMB = read(s, "fileMinFreeMB", static_cast<qulonglong>(d.file.minFreeMB));
    c.zmq.enabled = read(s, "zmqEnabled", d.zmq.enabled);
    c.zmq.endpoint = readString(s, "zmqEndpoint", d.zmq.endpoint);
    c.zmq.socket = readEnum(s, "zmqSocket", d.zmq.socket);
    c.zmq.bind = read(s, "zmqBind", d.zmq.bind);
    c.zmq.topic = readString(s, "zmqTopic", d.zmq.topic);
    c.zmq.metadataHeader = read(s, "zmqMetadataHeader", d.zmq.metadataHeader);
    c.zmq.tsPacketsPerMessage = read(s, "zmqTsPacketsPerMessage", d.zmq.tsPacketsPerMessage);
    c.zmq.sendHwm = read(s, "zmqSendHwm", d.zmq.sendHwm);
    s.endGroup();

    // Network outputs as URLs; a leading '-' marks a disabled one
    const QStringList urls = s.value(QStringLiteral("network/outputs")).toStringList();
    for (const QString &u : urls) {
        tbs::TsNetworkOutput o;
        const bool off = u.startsWith(QLatin1Char('-'));
        if (tbs::parseNetworkUrl((off ? u.mid(1) : u).toStdString(), &o)) {
            o.enabled = !off;
            c.network.push_back(o);
        }
    }
    return c;
}

void save(QSettings &s, const tbs::ReceiverConfig &c)
{
    s.beginGroup(QStringLiteral("tuner"));
    writeEnum(s, "deviceKind", c.device.kind);
    s.setValue(QStringLiteral("adapter"), c.device.adapter);
    s.setValue(QStringLiteral("frontend"), c.device.frontend);
    s.setValue(QStringLiteral("demux"), c.device.demux);
    s.setValue(QStringLiteral("bdaIndex"), c.device.bdaIndex);
    writeString(s, "bdaDevicePath", c.device.bdaDevicePath);
    writeString(s, "replayFile", c.device.replayFile);
    s.setValue(QStringLiteral("replayRateMbps"), c.device.replayRateMbps);
    s.setValue(QStringLiteral("replayLoop"), c.device.replayLoop);

    const tbs::TuningParams &t = c.tuning;
    s.setValue(QStringLiteral("frequencyMHz"), t.frequencyMHz);
    s.setValue(QStringLiteral("symbolRateKsps"), t.symbolRateKsps);
    writeEnum(s, "deliverySystem", t.deliverySystem);
    writeEnum(s, "modulation", t.modulation);
    writeEnum(s, "lnbType", t.lnbType);
    s.setValue(QStringLiteral("lnbLo1MHz"), t.lnbLo1MHz);
    s.setValue(QStringLiteral("lnbLo2MHz"), t.lnbLo2MHz);
    s.setValue(QStringLiteral("lnbSwitchMHz"), t.lnbSwitchMHz);
    writeEnum(s, "polarization", t.polarization);
    writeEnum(s, "tone", t.tone);
    s.setValue(QStringLiteral("diseqcPort"), t.diseqcPort);
    s.setValue(QStringLiteral("inputStreamId"), t.inputStreamId);
    writeEnum(s, "plsMode", t.plsMode);
    s.setValue(QStringLiteral("plsCode"), t.plsCode);
    writeEnum(s, "captureMode", c.capture);
    s.endGroup();

    s.beginGroup(QStringLiteral("output"));
    s.setValue(QStringLiteral("fileEnabled"), c.file.enabled);
    writeString(s, "filePath", c.file.path);
    s.setValue(QStringLiteral("fileTimestampSuffix"), c.file.timestampSuffix);
    writeEnum(s, "bbFileFormat", c.file.bbFormat);
    s.setValue(QStringLiteral("fileSplitSizeMB"), static_cast<qulonglong>(c.file.splitSizeMB));
    s.setValue(QStringLiteral("fileSplitMinutes"), c.file.splitMinutes);
    s.setValue(QStringLiteral("fileKeepFiles"), c.file.keepFiles);
    s.setValue(QStringLiteral("fileMinFreeMB"), static_cast<qulonglong>(c.file.minFreeMB));
    s.setValue(QStringLiteral("zmqEnabled"), c.zmq.enabled);
    writeString(s, "zmqEndpoint", c.zmq.endpoint);
    writeEnum(s, "zmqSocket", c.zmq.socket);
    s.setValue(QStringLiteral("zmqBind"), c.zmq.bind);
    writeString(s, "zmqTopic", c.zmq.topic);
    s.setValue(QStringLiteral("zmqMetadataHeader"), c.zmq.metadataHeader);
    s.setValue(QStringLiteral("zmqTsPacketsPerMessage"), c.zmq.tsPacketsPerMessage);
    s.setValue(QStringLiteral("zmqSendHwm"), c.zmq.sendHwm);
    s.endGroup();

    QStringList urls;
    for (const tbs::TsNetworkOutput &o : c.network)
        urls << (o.enabled ? QString() : QStringLiteral("-")) + QString::fromStdString(tbs::networkUrl(o));
    s.setValue(QStringLiteral("network/outputs"), urls);
}

tbs::bb::ProcessorConfig loadProcessing(QSettings &s)
{
    const tbs::bb::ProcessorConfig d;
    tbs::bb::ProcessorConfig c;
    s.beginGroup(QStringLiteral("processing"));
    c.pcap.enabled = read(s, "pcapEnabled", d.pcap.enabled);
    c.pcap.path = readString(s, "pcapPath", d.pcap.path);
    c.pcap.timestampSuffix = read(s, "pcapTimestampSuffix", d.pcap.timestampSuffix);
    c.pcap.ethernetFraming = read(s, "pcapEthernet", d.pcap.ethernetFraming);
    c.pcap.ipOnly = read(s, "pcapIpOnly", d.pcap.ipOnly);
    c.pcap.format = readEnum(s, "pcapFormat", d.pcap.format);
    c.tun.enabled = read(s, "tunEnabled", d.tun.enabled);
    c.tun.name = readString(s, "tunName", d.tun.name);
    c.tun.tap = read(s, "tunTap", d.tun.tap);
    if (!tbs::bb::parseGenericDecoder(readString(s, "genericDecoder", tbs::bb::genericDecoderId(d.genericDecoder)),
                                      &c.genericDecoder))
        c.genericDecoder = d.genericDecoder;
    s.endGroup();
    return c;
}

void saveProcessing(QSettings &s, const tbs::bb::ProcessorConfig &c)
{
    s.beginGroup(QStringLiteral("processing"));
    s.setValue(QStringLiteral("pcapEnabled"), c.pcap.enabled);
    writeString(s, "pcapPath", c.pcap.path);
    s.setValue(QStringLiteral("pcapTimestampSuffix"), c.pcap.timestampSuffix);
    s.setValue(QStringLiteral("pcapEthernet"), c.pcap.ethernetFraming);
    s.setValue(QStringLiteral("pcapIpOnly"), c.pcap.ipOnly);
    writeEnum(s, "pcapFormat", c.pcap.format);
    s.setValue(QStringLiteral("tunEnabled"), c.tun.enabled);
    writeString(s, "tunName", c.tun.name);
    s.setValue(QStringLiteral("tunTap"), c.tun.tap);
    writeString(s, "genericDecoder", tbs::bb::genericDecoderId(c.genericDecoder));
    s.endGroup();
}

tbs::MonitorConfig loadMonitor(QSettings &s)
{
    const tbs::MonitorConfig d;
    tbs::MonitorConfig c;
    s.beginGroup(QStringLiteral("monitor"));
    c.noLock = read(s, "noLock", d.noLock);
    c.noLockHoldSec = read(s, "noLockHoldSec", d.noLockHoldSec);
    c.lowCnr = read(s, "lowCnr", d.lowCnr);
    c.cnrMinDb = read(s, "cnrMinDb", d.cnrMinDb);
    c.cnrHoldSec = read(s, "cnrHoldSec", d.cnrHoldSec);
    c.lowLevel = read(s, "lowLevel", d.lowLevel);
    c.levelMinDbm = read(s, "levelMinDbm", d.levelMinDbm);
    c.levelHoldSec = read(s, "levelHoldSec", d.levelHoldSec);
    c.ccErrors = read(s, "ccErrors", d.ccErrors);
    c.ccMaxPerWindow = read(s, "ccMaxPerWindow", d.ccMaxPerWindow);
    c.bbCrcErrors = read(s, "bbCrcErrors", d.bbCrcErrors);
    c.bbCrcMaxPerWindow = read(s, "bbCrcMaxPerWindow", d.bbCrcMaxPerWindow);
    c.windowSec = read(s, "windowSec", d.windowSec);
    c.lowBitrate = read(s, "lowBitrate", d.lowBitrate);
    c.bitrateMinMbps = read(s, "bitrateMinMbps", d.bitrateMinMbps);
    c.bitrateHoldSec = read(s, "bitrateHoldSec", d.bitrateHoldSec);
    c.noData = read(s, "noData", d.noData);
    c.noDataHoldSec = read(s, "noDataHoldSec", d.noDataHoldSec);
    c.drops = read(s, "drops", d.drops);
    c.outputErrors = read(s, "outputErrors", d.outputErrors);
    c.command = readString(s, "command", d.command);
    s.endGroup();
    return c;
}

void saveMonitor(QSettings &s, const tbs::MonitorConfig &c)
{
    s.beginGroup(QStringLiteral("monitor"));
    s.setValue(QStringLiteral("noLock"), c.noLock);
    s.setValue(QStringLiteral("noLockHoldSec"), c.noLockHoldSec);
    s.setValue(QStringLiteral("lowCnr"), c.lowCnr);
    s.setValue(QStringLiteral("cnrMinDb"), c.cnrMinDb);
    s.setValue(QStringLiteral("cnrHoldSec"), c.cnrHoldSec);
    s.setValue(QStringLiteral("lowLevel"), c.lowLevel);
    s.setValue(QStringLiteral("levelMinDbm"), c.levelMinDbm);
    s.setValue(QStringLiteral("levelHoldSec"), c.levelHoldSec);
    s.setValue(QStringLiteral("ccErrors"), c.ccErrors);
    s.setValue(QStringLiteral("ccMaxPerWindow"), c.ccMaxPerWindow);
    s.setValue(QStringLiteral("bbCrcErrors"), c.bbCrcErrors);
    s.setValue(QStringLiteral("bbCrcMaxPerWindow"), c.bbCrcMaxPerWindow);
    s.setValue(QStringLiteral("windowSec"), c.windowSec);
    s.setValue(QStringLiteral("lowBitrate"), c.lowBitrate);
    s.setValue(QStringLiteral("bitrateMinMbps"), c.bitrateMinMbps);
    s.setValue(QStringLiteral("bitrateHoldSec"), c.bitrateHoldSec);
    s.setValue(QStringLiteral("noData"), c.noData);
    s.setValue(QStringLiteral("noDataHoldSec"), c.noDataHoldSec);
    s.setValue(QStringLiteral("drops"), c.drops);
    s.setValue(QStringLiteral("outputErrors"), c.outputErrors);
    writeString(s, "command", c.command);
    s.endGroup();
}

} // namespace SettingsStore
