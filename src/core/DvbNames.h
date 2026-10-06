#pragma once

#include <QString>

// Human readable names for the linux-dvb enum values reported by the frontend.
// The tables follow the TBS linux_media headers, which extend the mainline enums
// with DVB-S2X code rates and constellations.
namespace DvbNames {

QString deliverySystem(int v);
QString modulation(int v);
QString fec(int v);
QString rolloff(int v);
QString pilot(int v);
QString statusFlags(uint32_t flags);

} // namespace DvbNames
