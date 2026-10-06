#pragma once

#include <cstddef>
#include <cstdint>

// Receives a copy of the transport stream (e.g. the video preview decoder).
// consumeTs() is called on the ReceiverEngine worker thread and must not block.
class ITsConsumer
{
public:
    virtual ~ITsConsumer() = default;
    virtual void consumeTs(const uint8_t *data, size_t len) = 0;
};
