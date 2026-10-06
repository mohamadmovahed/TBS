#include "TunerConfig.h"
#include "PlsUtil.h"

#include <QSettings>
#include <cmath>

bool TunerConfig::isHighBand() const
{
    return lnbType == LnbType::Universal && frequencyMHz >= lnbSwitchMHz;
}

uint32_t TunerConfig::intermediateFrequencyKHz() const
{
    double ifMHz = frequencyMHz;
    switch (lnbType) {
    case LnbType::Direct:
        break;
    case LnbType::Universal:
        ifMHz = std::fabs(frequencyMHz - (isHighBand() ? lnbLo2MHz : lnbLo1MHz));
        break;
    case LnbType::SingleLo:
        ifMHz = std::fabs(frequencyMHz - lnbLo1MHz);
        break;
    }
    return static_cast<uint32_t>(std::lround(ifMHz * 1000.0));
}

bool TunerConfig::toneOn() const
{
    switch (tone) {
    case ToneMode::On:
        return true;
    case ToneMode::Off:
        return false;
    case ToneMode::Auto:
        break;
    }
    return isHighBand();
}

uint32_t TunerConfig::goldCode() const
{
    switch (plsMode) {
    case PlsMode::Root:
        return PlsUtil::rootToGold(plsCode);
    case PlsMode::Gold:
    case PlsMode::Combo:
        break;
    }
    return plsCode & 0x3FFFF;
}

uint32_t TunerConfig::dvbStreamId() const
{
    if (inputStreamId < 0)
        return ~0u;
    return (static_cast<uint32_t>(inputStreamId) & 0xFF)
        | ((plsCode & 0x3FFFF) << 8)
        | ((static_cast<uint32_t>(plsMode) & 0x3) << 26);
}

QString TunerConfig::describe() const
{
    static const char *pols[] = {"off", "V/R", "H/L"};
    QString s = QStringLiteral("%1 MHz %2 %3 kS/s, IF %4 MHz, tone %5")
                    .arg(frequencyMHz, 0, 'f', 3)
                    .arg(QLatin1String(pols[static_cast<int>(polarization)]))
                    .arg(symbolRateKsps, 0, 'f', 1)
                    .arg(intermediateFrequencyKHz() / 1000.0, 0, 'f', 3)
                    .arg(toneOn() ? QStringLiteral("on") : QStringLiteral("off"));
    if (diseqcPort >= 0)
        s += QStringLiteral(", DiSEqC %1").arg(QChar('A' + diseqcPort));
    if (inputStreamId >= 0)
        s += QStringLiteral(", ISI %1").arg(inputStreamId);
    if (goldCode() != 0)
        s += QStringLiteral(", PLS gold %1").arg(goldCode());
    return s;
}

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
} // namespace

void TunerConfig::load(QSettings &s)
{
    const TunerConfig d;
    s.beginGroup(QStringLiteral("tuner"));
    deviceKind = readEnum(s, "deviceKind", d.deviceKind);
    adapter = s.value(QStringLiteral("adapter"), d.adapter).toInt();
    frontend = s.value(QStringLiteral("frontend"), d.frontend).toInt();
    demux = s.value(QStringLiteral("demux"), d.demux).toInt();
    replayFile = s.value(QStringLiteral("replayFile"), d.replayFile).toString();
    replayRateMbps = s.value(QStringLiteral("replayRateMbps"), d.replayRateMbps).toDouble();
    replayLoop = s.value(QStringLiteral("replayLoop"), d.replayLoop).toBool();
    frequencyMHz = s.value(QStringLiteral("frequencyMHz"), d.frequencyMHz).toDouble();
    symbolRateKsps = s.value(QStringLiteral("symbolRateKsps"), d.symbolRateKsps).toDouble();
    deliverySystem = readEnum(s, "deliverySystem", d.deliverySystem);
    modulation = readEnum(s, "modulation", d.modulation);
    lnbType = readEnum(s, "lnbType", d.lnbType);
    lnbLo1MHz = s.value(QStringLiteral("lnbLo1MHz"), d.lnbLo1MHz).toDouble();
    lnbLo2MHz = s.value(QStringLiteral("lnbLo2MHz"), d.lnbLo2MHz).toDouble();
    lnbSwitchMHz = s.value(QStringLiteral("lnbSwitchMHz"), d.lnbSwitchMHz).toDouble();
    polarization = readEnum(s, "polarization", d.polarization);
    tone = readEnum(s, "tone", d.tone);
    diseqcPort = s.value(QStringLiteral("diseqcPort"), d.diseqcPort).toInt();
    inputStreamId = s.value(QStringLiteral("inputStreamId"), d.inputStreamId).toInt();
    plsMode = readEnum(s, "plsMode", d.plsMode);
    plsCode = s.value(QStringLiteral("plsCode"), d.plsCode).toUInt();
    captureMode = readEnum(s, "captureMode", d.captureMode);
    s.endGroup();

    s.beginGroup(QStringLiteral("output"));
    fileEnabled = s.value(QStringLiteral("fileEnabled"), d.fileEnabled).toBool();
    filePath = s.value(QStringLiteral("filePath"), d.filePath).toString();
    fileTimestampSuffix = s.value(QStringLiteral("fileTimestampSuffix"), d.fileTimestampSuffix).toBool();
    bbFileFormat = readEnum(s, "bbFileFormat", d.bbFileFormat);
    zmqEnabled = s.value(QStringLiteral("zmqEnabled"), d.zmqEnabled).toBool();
    zmqEndpoint = s.value(QStringLiteral("zmqEndpoint"), d.zmqEndpoint).toString();
    zmqSocket = readEnum(s, "zmqSocket", d.zmqSocket);
    zmqBind = s.value(QStringLiteral("zmqBind"), d.zmqBind).toBool();
    zmqTopic = s.value(QStringLiteral("zmqTopic"), d.zmqTopic).toString();
    zmqMetadataHeader = s.value(QStringLiteral("zmqMetadataHeader"), d.zmqMetadataHeader).toBool();
    zmqTsPacketsPerMessage = s.value(QStringLiteral("zmqTsPacketsPerMessage"), d.zmqTsPacketsPerMessage).toInt();
    zmqSendHwm = s.value(QStringLiteral("zmqSendHwm"), d.zmqSendHwm).toInt();
    videoPreview = s.value(QStringLiteral("videoPreview"), d.videoPreview).toBool();
    s.endGroup();
}

void TunerConfig::save(QSettings &s) const
{
    s.beginGroup(QStringLiteral("tuner"));
    writeEnum(s, "deviceKind", deviceKind);
    s.setValue(QStringLiteral("adapter"), adapter);
    s.setValue(QStringLiteral("frontend"), frontend);
    s.setValue(QStringLiteral("demux"), demux);
    s.setValue(QStringLiteral("replayFile"), replayFile);
    s.setValue(QStringLiteral("replayRateMbps"), replayRateMbps);
    s.setValue(QStringLiteral("replayLoop"), replayLoop);
    s.setValue(QStringLiteral("frequencyMHz"), frequencyMHz);
    s.setValue(QStringLiteral("symbolRateKsps"), symbolRateKsps);
    writeEnum(s, "deliverySystem", deliverySystem);
    writeEnum(s, "modulation", modulation);
    writeEnum(s, "lnbType", lnbType);
    s.setValue(QStringLiteral("lnbLo1MHz"), lnbLo1MHz);
    s.setValue(QStringLiteral("lnbLo2MHz"), lnbLo2MHz);
    s.setValue(QStringLiteral("lnbSwitchMHz"), lnbSwitchMHz);
    writeEnum(s, "polarization", polarization);
    writeEnum(s, "tone", tone);
    s.setValue(QStringLiteral("diseqcPort"), diseqcPort);
    s.setValue(QStringLiteral("inputStreamId"), inputStreamId);
    writeEnum(s, "plsMode", plsMode);
    s.setValue(QStringLiteral("plsCode"), plsCode);
    writeEnum(s, "captureMode", captureMode);
    s.endGroup();

    s.beginGroup(QStringLiteral("output"));
    s.setValue(QStringLiteral("fileEnabled"), fileEnabled);
    s.setValue(QStringLiteral("filePath"), filePath);
    s.setValue(QStringLiteral("fileTimestampSuffix"), fileTimestampSuffix);
    writeEnum(s, "bbFileFormat", bbFileFormat);
    s.setValue(QStringLiteral("zmqEnabled"), zmqEnabled);
    s.setValue(QStringLiteral("zmqEndpoint"), zmqEndpoint);
    writeEnum(s, "zmqSocket", zmqSocket);
    s.setValue(QStringLiteral("zmqBind"), zmqBind);
    s.setValue(QStringLiteral("zmqTopic"), zmqTopic);
    s.setValue(QStringLiteral("zmqMetadataHeader"), zmqMetadataHeader);
    s.setValue(QStringLiteral("zmqTsPacketsPerMessage"), zmqTsPacketsPerMessage);
    s.setValue(QStringLiteral("zmqSendHwm"), zmqSendHwm);
    s.setValue(QStringLiteral("videoPreview"), videoPreview);
    s.endGroup();
}
