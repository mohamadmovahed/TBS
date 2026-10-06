#pragma once

#include "BbHeader.h"
#include "Types.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tbs {

// Destination for captured data. Sinks are opened, fed and closed on the
// Receiver worker thread. Applications can implement their own and register
// them with Receiver::addSink().
class TBSDVB_API IOutputSink
{
public:
    virtual ~IOutputSink();

    virtual bool open(std::string *error) = 0;
    virtual void close() = 0;

    // Whether the sink wants aligned 188-byte packets (TS capture, or the raw pseudo-TS in BBFrame mode)
    virtual bool acceptsTs(CaptureMode mode) const = 0;
    // Whether the sink wants reassembled BBFrames (BBFrame mode)
    virtual bool acceptsBbFrames(CaptureMode mode) const = 0;

    virtual void writeTs(const uint8_t *packets, size_t count) = 0;
    virtual void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) = 0;
};

class TBSDVB_API FileSink : public IOutputSink
{
public:
    FileSink(const FileOutputOptions &opt, CaptureMode mode);
    ~FileSink() override;

    bool open(std::string *error) override;
    void close() override;
    bool acceptsTs(CaptureMode mode) const override;
    bool acceptsBbFrames(CaptureMode mode) const override;
    void writeTs(const uint8_t *packets, size_t count) override;
    void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) override;

    uint64_t bytesWritten() const { return m_bytes; }
    const std::string &fileName() const { return m_path; }
    const std::string &lastError() const { return m_error; }

    // Final file name: default location, extension and optional timestamp suffix applied
    static std::string resolvePath(const FileOutputOptions &opt, CaptureMode mode);

private:
    void write(const void *data, size_t len);

    FileOutputOptions m_opt;
    CaptureMode m_mode;
    std::FILE *m_file = nullptr;
    std::vector<char> m_buffer;
    std::string m_path;
    std::string m_error;
    uint64_t m_bytes = 0;
};

// Publishes data on a ZeroMQ PUB or PUSH socket.
//
// One message per unit, optional topic frame first:
//   [topic]   if configured (multipart, ZMQ_SNDMORE)
//   [header]  if metadataHeader, 20 bytes little endian:
//               char[4] "TBS1", u8 type (0 = TS, 1 = BBFrame), u8 flags, u16 reserved,
//               u32 sequence, u64 timestamp (us since epoch)
//   payload   N x 188-byte TS packets, or one BBFrame (BBHeader + data field)
class TBSDVB_API ZmqSink : public IOutputSink
{
public:
    static constexpr size_t kHeaderSize = 20;

    ZmqSink(const ZmqOutputOptions &opt, CaptureMode mode);
    ~ZmqSink() override;

    static bool available(); // false if the library was built without libzmq

    bool open(std::string *error) override;
    void close() override;
    bool acceptsTs(CaptureMode mode) const override { return mode == CaptureMode::TransportStream; }
    bool acceptsBbFrames(CaptureMode mode) const override { return mode == CaptureMode::BBFrame; }
    void writeTs(const uint8_t *packets, size_t count) override;
    void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) override;

    uint64_t messages() const { return m_messages; }
    uint64_t dropped() const { return m_dropped; }

private:
    void send(uint8_t type, const uint8_t *payload, size_t len);

    ZmqOutputOptions m_opt;
    CaptureMode m_mode;
    void *m_ctx = nullptr;
    void *m_sock = nullptr;
    std::vector<uint8_t> m_batch;
    std::vector<uint8_t> m_msg;
    size_t m_batchPackets = 7;
    uint32_t m_seq = 0;
    uint64_t m_messages = 0;
    uint64_t m_dropped = 0;
};

} // namespace tbs
