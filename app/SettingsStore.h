#pragma once

#include <tbsdvb/Types.h>

class QSettings;

// Persists a tbs::ReceiverConfig in QSettings.
namespace SettingsStore {

tbs::ReceiverConfig load(QSettings &s);
void save(QSettings &s, const tbs::ReceiverConfig &cfg);

} // namespace SettingsStore
