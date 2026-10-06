#include "DeviceFactory.h"

#include "FileReplayDevice.h"
#ifdef TBS_HAVE_LINUX_DVB
#include "LinuxDvbDevice.h"
#endif

#include <QFile>

namespace DeviceFactory {

QList<DeviceInfo> enumerate()
{
    QList<DeviceInfo> list;
#ifdef TBS_HAVE_LINUX_DVB
    list += LinuxDvbDevice::enumerate();
#endif
    DeviceInfo replay;
    replay.kind = DeviceKind::FileReplay;
    replay.label = QStringLiteral("File replay (recorded TS / pseudo-TS)");
    list.append(replay);
    return list;
}

std::unique_ptr<ITunerDevice> create(const TunerConfig &cfg, QString *error)
{
    switch (cfg.deviceKind) {
    case DeviceKind::FileReplay:
        return std::make_unique<FileReplayDevice>();
    case DeviceKind::LinuxDvb:
#ifdef TBS_HAVE_LINUX_DVB
        return std::make_unique<LinuxDvbDevice>();
#else
        if (error)
            *error = QStringLiteral("Direct card access is only implemented for the Linux DVB API on this build. "
                                    "Use 'File replay' or run on Linux with the TBS open source driver.");
        return nullptr;
#endif
    }
    if (error)
        *error = QStringLiteral("Unknown device type");
    return nullptr;
}

int stid135BbFrameParameter()
{
    QFile f(QStringLiteral("/sys/module/stid135/parameters/bbframe"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const int v = f.readAll().trimmed().toInt(&ok);
    return ok ? v : -1;
}

} // namespace DeviceFactory
