#pragma once

#include "core/BbHeader.h"

#include <QString>
#include <cstddef>
#include <cstdint>

// Destination for captured data. Sinks are created, used and destroyed on the
// ReceiverEngine worker thread.
class IOutputSink
{
public:
    virtual ~IOutputSink() = default;

    virtual bool open(QString *error) = 0;
    virtual void close() = 0;

    // Whether the sink wants aligned 188-byte packets (TS capture or raw pseudo-TS dump)
    virtual bool acceptsTs() const = 0;
    // Whether the sink wants reassembled BBFrames
    virtual bool acceptsBbFrames() const = 0;

    virtual void writeTs(const uint8_t *packets, size_t count) = 0;
    virtual void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) = 0;
};
