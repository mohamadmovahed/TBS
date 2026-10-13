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
    s.endGroup();
}

} // namespace SettingsStore
