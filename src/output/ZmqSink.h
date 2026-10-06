#pragma once

#include "IOutputSink.h"
#include "core/TunerConfig.h"

#include <vector>

// Publishes captured data on a ZeroMQ PUB or PUSH socket.
//
// Message layout (one ZeroMQ message per unit, optional topic frame first):
//   [topic frame]        only if a topic is configured (sent with ZMQ_SNDMORE)
//   [20-byte header]     only if "metadata header" is enabled, little endian:
//                          char[4] "TBS1", u8 type (0 = TS, 1 = BBFrame), u8 flags,
//                          u16 reserved, u32 sequence, u64 timestamp (us since epoch)
//   payload              N x 188-byte TS packets, or one BBFrame (BBHeader + data field)
class ZmqSink : public IOutputSink
{
public:
    static constexpr size_t kHeaderSize = 20;

    explicit ZmqSink(const TunerConfig &cfg);
    ~ZmqSink() override;

    bool open(QString *error) override;
    void close() override;
    bool acceptsTs() const override { return m_cfg.captureMode == CaptureMode::TransportStream; }
    bool acceptsBbFrames() const override { return m_cfg.captureMode == CaptureMode::BBFrame; }
    void writeTs(const uint8_t *packets, size_t count) override;
    void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) override;

    uint64_t messages() const { return m_messages; }
    uint64_t dropped() const { return m_dropped; }

private:
    void send(uint8_t type, const uint8_t *payload, size_t len);

    TunerConfig m_cfg;
    void *m_ctx = nullptr;
    void *m_sock = nullptr;
    QByteArray m_topic;
    std::vector<uint8_t> m_batch;
    std::vector<uint8_t> m_msg;
    size_t m_batchPackets = 7;
    uint32_t m_seq = 0;
    uint64_t m_messages = 0;
    uint64_t m_dropped = 0;
};
