#include "tbsdvb/Device.h"

#include "FileReplayDevice.h"
#include "Util.h"
#if defined(TBSDVB_HAVE_LINUX_DVB)
#include "platform/LinuxDvbDevice.h"
#endif
#if defined(TBSDVB_HAVE_WIN_BDA)
#include "platform/WinBdaDevice.h"
#endif

#include <cstdio>

namespace tbs {

ITunerDevice::~ITunerDevice() = default;

namespace Devices {

std::vector<DeviceInfo> enumerate(bool includeReplay)
{
    std::vector<DeviceInfo> list;
#if defined(TBSDVB_HAVE_LINUX_DVB)
    for (auto &d : LinuxDvbDevice::enumerate())
        list.push_back(d);
#endif
#if defined(TBSDVB_HAVE_WIN_BDA)
    for (auto &d : WinBdaDevice::enumerate())
        list.push_back(d);
#endif
    if (includeReplay) {
        DeviceInfo replay;
        replay.kind = DeviceKind::FileReplay;
        replay.name = "File replay (recorded TS / pseudo-TS)";
        list.push_back(replay);
    }
    return list;
}

bool isSupported(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::FileReplay:
        return true;
    case DeviceKind::LinuxDvb:
#if defined(TBSDVB_HAVE_LINUX_DVB)
        return true;
#else
        return false;
#endif
    case DeviceKind::WindowsBda:
#if defined(TBSDVB_HAVE_WIN_BDA)
        return true;
#else
        return false;
#endif
    }
    return false;
}

std::unique_ptr<ITunerDevice> create(DeviceKind kind, std::string *error)
{
    switch (kind) {
    case DeviceKind::FileReplay:
        return std::make_unique<FileReplayDevice>();
    case DeviceKind::LinuxDvb:
#if defined(TBSDVB_HAVE_LINUX_DVB)
        return std::make_unique<LinuxDvbDevice>();
#else
        break;
#endif
    case DeviceKind::WindowsBda:
#if defined(TBSDVB_HAVE_WIN_BDA)
        return std::make_unique<WinBdaDevice>();
#else
        break;
#endif
    }
    if (error)
        *error = "This device type is not supported on this platform/build";
    return nullptr;
}

int stid135BbFrameParameter()
{
    std::FILE *f = std::fopen("/sys/module/stid135/parameters/bbframe", "r");
    if (!f)
        return -1;
    int v = -1;
    if (std::fscanf(f, "%d", &v) != 1)
        v = -1;
    std::fclose(f);
    return v;
}

} // namespace Devices
} // namespace tbs
