#pragma once

#include "ITunerDevice.h"

#include <QList>
#include <QString>
#include <memory>

struct DeviceInfo
{
    DeviceKind kind = DeviceKind::LinuxDvb;
    int adapter = -1;
    int frontend = -1;
    QString label;
};

namespace DeviceFactory {

// Lists the DVB frontends present on this machine (Linux) followed by the file replay entry.
QList<DeviceInfo> enumerate();

std::unique_ptr<ITunerDevice> create(const TunerConfig &cfg, QString *error);

// Value of /sys/module/stid135/parameters/bbframe, or -1 if not available.
int stid135BbFrameParameter();

} // namespace DeviceFactory
