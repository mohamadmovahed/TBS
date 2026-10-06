#include "tbsdvb/Sink.h"

#include <chrono>
#include <algorithm>
#include <cstring>

#ifdef TBSDVB_HAVE_ZMQ
#include <zmq.h>
#endif

namespace tbs {

bool ZmqSink::available()
{
#ifdef TBSDVB_HAVE_ZMQ
    return true;
#else
    return false;
#endif
}

ZmqSink::ZmqSink(const ZmqOutputOptions &opt, CaptureMode mode)
    : m_opt(opt)
    , m_mode(mode)
    , m_batchPackets(static_cast<size_t>(std::max(1, opt.tsPacketsPerMessage)))
{
    m_batch.reserve(m_batchPackets * 188);
}

ZmqSink::~ZmqSink()
{
    close();
}

bool ZmqSink::open(std::string *error)
{
#ifdef TBSDVB_HAVE_ZMQ
    m_ctx = zmq_ctx_new();
    if (!m_ctx) {
        if (error)
            *error = "zmq_ctx_new failed";
        return false;
    }
    m_sock = zmq_socket(m_ctx, m_opt.socket == ZmqSocketKind::Pub ? ZMQ_PUB : ZMQ_PUSH);
    if (!m_sock) {
        if (error)
            *error = std::string("zmq_socket failed: ") + zmq_strerror(zmq_errno());
        close();
        return false;
    }
    const int hwm = std::max(1, m_opt.sendHwm);
    const int linger = 1000; // let queued messages go out when capture stops
    zmq_setsockopt(m_sock, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(m_sock, ZMQ_LINGER, &linger, sizeof(linger));

    const int rc = m_opt.bind ? zmq_bind(m_sock, m_opt.endpoint.c_str()) : zmq_connect(m_sock, m_opt.endpoint.c_str());
    if (rc != 0) {
        if (error)
            *error = std::string("ZeroMQ ") + (m_opt.bind ? "bind" : "connect") + " to '" + m_opt.endpoint
                + "' failed: " + zmq_strerror(zmq_errno());
        close();
        return false;
    }
    return true;
#else
    if (error)
        *error = "This build has no ZeroMQ support (rebuild with libzmq installed)";
    return false;
#endif
}

void ZmqSink::close()
{
#ifdef TBSDVB_HAVE_ZMQ
    if (m_sock && !m_batch.empty())
        send(0, m_batch.data(), m_batch.size());
    m_batch.clear();
    if (m_sock) {
        zmq_close(m_sock);
        m_sock = nullptr;
    }
    if (m_ctx) {
        zmq_ctx_term(m_ctx);
        m_ctx = nullptr;
    }
#endif
}

void ZmqSink::send(uint8_t type, const uint8_t *payload, size_t len)
{
#ifdef TBSDVB_HAVE_ZMQ
    if (!m_sock)
        return;

    const uint8_t *data = payload;
    size_t size = len;
    if (m_opt.metadataHeader) {
        m_msg.resize(kHeaderSize + len);
        uint8_t *h = m_msg.data();
        std::memcpy(h, "TBS1", 4);
        h[4] = type;
        h[5] = 0;
        h[6] = h[7] = 0;
        const uint32_t seq = m_seq;
        const uint64_t ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        for (int i = 0; i < 4; ++i)
            h[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
        for (int i = 0; i < 8; ++i)
            h[12 + i] = static_cast<uint8_t>(ts >> (8 * i));
        std::memcpy(h + kHeaderSize, payload, len);
        data = m_msg.data();
        size = m_msg.size();
    }
    ++m_seq;

    if (!m_opt.topic.empty()) {
        if (zmq_send(m_sock, m_opt.topic.data(), m_opt.topic.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT) < 0) {
            ++m_dropped;
            return;
        }
    }
    if (zmq_send(m_sock, data, size, ZMQ_DONTWAIT) < 0) {
        ++m_dropped;
        return;
    }
    ++m_messages;
#else
    (void)type;
    (void)payload;
    (void)len;
#endif
}

void ZmqSink::writeTs(const uint8_t *packets, size_t count)
{
    const size_t batchBytes = m_batchPackets * 188;
    while (count > 0) {
        if (m_batch.empty() && count >= m_batchPackets) {
            // Fast path: send directly from the input buffer
            send(0, packets, batchBytes);
            packets += batchBytes;
            count -= m_batchPackets;
            continue;
        }
        const size_t room = m_batchPackets - m_batch.size() / 188;
        const size_t take = std::min(room, count);
        m_batch.insert(m_batch.end(), packets, packets + take * 188);
        packets += take * 188;
        count -= take;
        if (m_batch.size() == batchBytes) {
            send(0, m_batch.data(), m_batch.size());
            m_batch.clear();
        }
    }
}

void ZmqSink::writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &)
{
    send(1, frame, len);
}

} // namespace tbs
