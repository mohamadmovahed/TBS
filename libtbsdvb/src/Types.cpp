#include "tbsdvb/Types.h"

#include "Util.h"
#include "tbsdvb/PlsUtil.h"

#include <cmath>

#ifndef TBSDVB_VERSION
#define TBSDVB_VERSION "0.0.0"
#endif

namespace tbs {

const char *version()
{
    return TBSDVB_VERSION;
}

bool TuningParams::isHighBand() const
{
    return lnbType == LnbType::Universal && frequencyMHz >= lnbSwitchMHz;
}

double TuningParams::activeLoMHz() const
{
    switch (lnbType) {
    case LnbType::Direct:
        return 0.0;
    case LnbType::Universal:
        return isHighBand() ? lnbLo2MHz : lnbLo1MHz;
    case LnbType::SingleLo:
        return lnbLo1MHz;
    }
    return 0.0;
}

uint32_t TuningParams::intermediateFrequencyKHz() const
{
    const double ifMHz = lnbType == LnbType::Direct ? frequencyMHz : std::fabs(frequencyMHz - activeLoMHz());
    return static_cast<uint32_t>(std::lround(ifMHz * 1000.0));
}

bool TuningParams::toneOn() const
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

uint32_t TuningParams::goldCode() const
{
    if (plsMode == PlsMode::Root)
        return PlsUtil::rootToGold(plsCode);
    return plsCode & 0x3FFFF;
}

uint32_t TuningParams::dvbStreamId() const
{
    if (inputStreamId < 0)
        return ~0u;
    return (static_cast<uint32_t>(inputStreamId) & 0xFF) | ((plsCode & 0x3FFFF) << 8)
        | ((static_cast<uint32_t>(plsMode) & 0x3) << 26);
}

std::string TuningParams::describe() const
{
    static const char *pols[] = {"off", "V/R", "H/L"};
    std::string s = util::format("%.3f MHz %s %.1f kS/s, IF %.3f MHz, tone %s", frequencyMHz,
                                 pols[static_cast<int>(polarization)], symbolRateKsps,
                                 intermediateFrequencyKHz() / 1000.0, toneOn() ? "on" : "off");
    if (diseqcPort >= 0)
        s += util::format(", DiSEqC %c", 'A' + diseqcPort);
    if (inputStreamId >= 0)
        s += util::format(", ISI %d", inputStreamId);
    if (goldCode() != 0)
        s += util::format(", PLS gold %u", goldCode());
    return s;
}

std::string DeviceInfo::label() const
{
    switch (kind) {
    case DeviceKind::LinuxDvb:
        return util::format("adapter%d/frontend%d: %s", adapter, frontend, name.c_str());
    case DeviceKind::WindowsBda:
        return util::format("BDA #%d: %s", index, name.c_str());
    case DeviceKind::FileReplay:
        break;
    }
    return name;
}

void DeviceInfo::applyTo(DeviceSelection &sel) const
{
    sel.kind = kind;
    switch (kind) {
    case DeviceKind::LinuxDvb:
        sel.adapter = adapter;
        sel.frontend = frontend;
        break;
    case DeviceKind::WindowsBda:
        sel.bdaIndex = index;
        sel.bdaDevicePath = path;
        break;
    case DeviceKind::FileReplay:
        break;
    }
}

} // namespace tbs
