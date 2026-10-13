#pragma once

#include "Processor.h"
#include "PsiTypes.h"
#include "export.h"

#include <tbsdvb/Receiver.h>
#include <tbsdvb/Types.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

// Tuning parameters for a transport described by a satellite delivery descriptor.
// LNB type and LOs, DiSEqC port and tone mode are taken from lnb.
TBSBBPROC_API TuningParams tuningFromDelivery(const SatelliteDelivery &sd, const TuningParams &lnb);
// All satellite transports of a NIT (those without a delivery descriptor are skipped)
TBSBBPROC_API std::vector<TuningParams> tuningFromNetwork(const NetworkInfo &nit, const TuningParams &lnb);
// Same carrier (within toleranceMHz, same polarization and input stream)
TBSBBPROC_API bool sameTransponder(const TuningParams &a, const TuningParams &b, double toleranceMHz = 2.0);

struct TBSBBPROC_API NetworkScanOptions
{
    int lockTimeoutMs = 4000;    // no lock within this time: transponder skipped
    int tableTimeoutMs = 12000;  // max. time to wait for PAT, SDT and NIT after lock
    bool followNit = true;       // add the transports listed in the NIT actual
    bool includeNitOther = false;// ... and in NIT other (other networks)
    bool sameOrbitalPosition = true; // only transports at the orbital position of the first NIT
    int maxTransponders = 400;
};

struct TBSBBPROC_API ScannedTransponder
{
    TuningParams tuning;
    bool locked = false;
    bool tablesComplete = false; // PAT, SDT actual (and NIT) seen before the timeout
    uint16_t tsid = 0;
    uint16_t onid = 0;
    uint16_t networkId = 0;
    std::string networkName;
    std::vector<ServiceInfo> services; // SDT actual of this transport stream
    std::string delivery;              // as announced in the NIT, if listed
    double seconds = 0.0;              // time spent on it
};

struct TBSBBPROC_API NetworkScanProgress
{
    int index = 0;      // transponder being scanned (0 based)
    int total = 0;      // known so far (grows while the NIT adds transports)
    std::string message;
};

struct TBSBBPROC_API NetworkScanResult
{
    std::vector<ScannedTransponder> transponders;
    uint16_t networkId = 0;
    std::string networkName;
    bool cancelled = false;

    size_t serviceCount() const;
};

// Scans transponder by transponder: tunes the running receiver to each, waits for lock and the
// SI tables, collects the services and (followNit) adds the transports listed in the NIT.
// rx must be running and feeding proc (onTsPackets/onBbFrame -> pushTs/pushBbFrame); proc is
// reset for each transponder. Blocks until done; set cancel to stop early. onTransponder is
// called after each transponder.
TBSBBPROC_API NetworkScanResult runNetworkScan(Receiver &rx, Processor &proc, const std::vector<TuningParams> &seeds,
                                               const NetworkScanOptions &opt = NetworkScanOptions(),
                                               const std::function<void(const NetworkScanProgress &)> &progress = {},
                                               const std::function<void(const ScannedTransponder &)> &onTransponder = {},
                                               const std::atomic<bool> *cancel = nullptr);

// Human readable summary, one line per transponder and service
TBSBBPROC_API std::string formatNetworkScan(const NetworkScanResult &r);

} // namespace bb
} // namespace tbs
