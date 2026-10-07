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

std::vector<DiagnosticItem> diagnose(const DeviceSelection &sel)
{
    using L = DiagnosticItem::Level;
    switch (sel.kind) {
    case DeviceKind::FileReplay: {
        std::FILE *f = util::openFile(sel.replayFile, "rb");
        if (f) {
            std::fclose(f);
            return {{L::Ok, "Replay file is readable: " + sel.replayFile, {}}};
        }
        return {{L::Error, "Replay file cannot be opened: " + sel.replayFile, "Check the path and permissions."}};
    }
    case DeviceKind::LinuxDvb:
#if defined(TBSDVB_HAVE_LINUX_DVB)
        return LinuxDvbDevice::diagnose(sel);
#else
        return {{L::Error, "This build has no Linux DVB support", "Linux DVB devices can only be used on Linux."}};
#endif
    case DeviceKind::WindowsBda: {
#if defined(TBSDVB_HAVE_WIN_BDA)
        std::vector<DiagnosticItem> out;
        const auto tuners = WinBdaDevice::enumerate();
        if (tuners.empty()) {
            out.push_back({L::Error, "No BDA tuner found",
                           "Install the TBS BDA driver for the card and check Device Manager (Sound, video and game "
                           "controllers / Media) for the TBS tuners. Reboot after installing."});
        }
        for (const auto &t : tuners)
            out.push_back({L::Ok, util::format("BDA tuner #%d: %s", t.index, t.name.c_str()), {}});
        if (!tuners.empty() && sel.bdaDevicePath.empty() && (sel.bdaIndex < 0 || sel.bdaIndex >= static_cast<int>(tuners.size())))
            out.push_back({L::Error, util::format("Selected BDA tuner #%d does not exist", sel.bdaIndex),
                           "Pick one of the tuners listed above."});
        return out;
#else
        return {{L::Error, "This build has no Windows BDA support", "BDA devices can only be used on Windows."}};
#endif
    }
    }
    return {};
}

std::string formatDiagnostics(const std::vector<DiagnosticItem> &items)
{
    std::string out;
    for (const auto &i : items) {
        const char *tag = i.level == DiagnosticItem::Level::Ok        ? "[ OK ] "
            : i.level == DiagnosticItem::Level::Info                  ? "[INFO] "
            : i.level == DiagnosticItem::Level::Warning               ? "[WARN] "
                                                                      : "[FAIL] ";
        out += tag + i.message + "\n";
        if (!i.hint.empty())
            out += "       -> " + i.hint + "\n";
    }
    return out;
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
