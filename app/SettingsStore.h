#pragma once

#include <tbsbbproc/Processor.h>
#include <tbsdvb/Types.h>

class QSettings;

// Persists a tbs::ReceiverConfig in QSettings.
namespace SettingsStore {

tbs::ReceiverConfig load(QSettings &s);
void save(QSettings &s, const tbs::ReceiverConfig &cfg);

tbs::bb::ProcessorConfig loadProcessing(QSettings &s);
void saveProcessing(QSettings &s, const tbs::bb::ProcessorConfig &cfg);

} // namespace SettingsStore
