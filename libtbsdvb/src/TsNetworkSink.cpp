#include "tbsdvb/Sink.h"

#include "NetSocket.h"
#include "Util.h"
#include "tbsdvb/BbFrameToTs.h"
#include "tbsdvb/Status.h"
#include "tbsdvb/TsFilter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

#ifdef TBSDVB_HAVE_SRT
#include <srt/srt.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif
#endif

namespace tbs {

namespace {

constexpr size_t kTsSize = 188;
constexpr size_t kRtpHeader = 12;

#ifdef TBSDVB_HAVE_SRT
// One SRT link: a caller that keeps (re)connecting, or a listener that accepts any number of clients.
class SrtLink
{
public:
    ~SrtLink() { close(); }

    bool open(const TsNetworkOutput &opt, std::string *error)
    {
        m_opt = opt;
        if (srt_startup() < 0) {
            if (error)
                *error = std::string("SRT startup failed: ") + srt_getlasterror_str();
            return false;
        }
        m_started = true;
        m_running = true;
        if (opt.srtMode == SrtMode::Listener) {
            m_listener = srt_create_socket();
            if (m_listener == SRT_INVALID_SOCK || !applyOptions(m_listener, error))
                return failOpen(error, "socket");
            const int yes = 1;
            srt_setsockflag(m_listener, SRTO_REUSEADDR, &yes, sizeof(yes));
            sockaddr_storage addr;
            int len = 0;
            if (!resolve(opt.host.empty() ? std::string() : opt.host, opt.port, true, &addr, &len, error))
                return failOpen(error, nullptr);
            if (srt_bind(m_listener, reinterpret_cast<sockaddr *>(&addr), len) == SRT_ERROR)
                return failOpen(error, "bind");
            if (srt_listen(m_listener, 8) == SRT_ERROR)
                return failOpen(error, "listen");
            setState(util::format("listening on port %u", static_cast<unsigned>(opt.port)));
            m_thread = std::thread([this] { listenerLoop(); });
        } else {
            sockaddr_storage addr;
            int len = 0;
            if (!resolve(opt.host, opt.port, false, &addr, &len, error))
                return failOpen(error, nullptr);
            setState("connecting");
            m_thread = std::thread([this, addr, len] { callerLoop(addr, len); });
        }
        return true;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            if (m_listener != SRT_INVALID_SOCK)
                srt_close(m_listener); // unblocks srt_accept
            if (m_connecting != SRT_INVALID_SOCK)
                srt_close(m_connecting);
            for (SRTSOCKET s : m_peers)
                srt_close(s);
            m_peers.clear();
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        m_listener = m_connecting = SRT_INVALID_SOCK;
        if (m_started)
            srt_cleanup();
        m_started = false;
    }

    // Sends one message to every peer. Returns the number of connected peers that did not take it.
    int send(const uint8_t *data, size_t len)
    {
        int failed = 0;
        bool lost = false;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_peers.size();) {
            if (srt_sendmsg2(m_peers[i], reinterpret_cast<const char *>(data), static_cast<int>(len), nullptr)
                != SRT_ERROR) {
                ++i;
                continue;
            }
            if (srt_getlasterror(nullptr) == SRT_EASYNCSND) { // buffer full, connection still up
                ++failed;
                ++i;
                continue;
            }
            // The peer went away: not a send error, the caller reconnects / the listener waits
            srt_close(m_peers[i]);
            m_peers.erase(m_peers.begin() + static_cast<std::ptrdiff_t>(i));
            lost = true;
        }
        if (lost) {
            updatePeerState();
            m_cv.notify_all();
        }
        return failed;
    }

    int peers() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_peers.size());
    }

    std::string state() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_state;
    }

private:
    bool failOpen(std::string *error, const char *what)
    {
        if (what && error)
            *error = std::string("SRT ") + what + ": " + srt_getlasterror_str();
        close();
        return false;
    }

    static bool resolve(const std::string &host, uint16_t port, bool passive, sockaddr_storage *out, int *len,
                        std::string *error)
    {
        addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = host.empty() ? AF_INET : AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        addrinfo *res = nullptr;
        const std::string service = std::to_string(port);
        const int rc = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &res);
        if (rc != 0 || !res) {
            if (error)
                *error = "cannot resolve \"" + host + "\": " + gai_strerror(rc);
            return false;
        }
        std::memset(out, 0, sizeof(*out));
        std::memcpy(out, res->ai_addr, res->ai_addrlen);
        *len = static_cast<int>(res->ai_addrlen);
        ::freeaddrinfo(res);
        return true;
    }

    bool applyOptions(SRTSOCKET s, std::string *error)
    {
        const int live = SRTT_LIVE;
        const int no = 0;
        const int latency = m_opt.srtLatencyMs;
        bool ok = srt_setsockflag(s, SRTO_TRANSTYPE, &live, sizeof(live)) != SRT_ERROR
                  && srt_setsockflag(s, SRTO_LATENCY, &latency, sizeof(latency)) != SRT_ERROR
                  && srt_setsockflag(s, SRTO_SNDSYN, &no, sizeof(no)) != SRT_ERROR;
        if (ok && !m_opt.srtPassphrase.empty())
            ok = srt_setsockflag(s, SRTO_PASSPHRASE, m_opt.srtPassphrase.c_str(),
                                 static_cast<int>(m_opt.srtPassphrase.size()))
                 != SRT_ERROR;
        if (ok && !m_opt.srtStreamId.empty() && m_opt.srtMode == SrtMode::Caller)
            ok = srt_setsockflag(s, SRTO_STREAMID, m_opt.srtStreamId.c_str(),
                                 static_cast<int>(m_opt.srtStreamId.size()))
                 != SRT_ERROR;
        if (!ok && error)
            *error = std::string("SRT options: ") + srt_getlasterror_str();
        return ok;
    }

    void setState(const std::string &s)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = s;
    }

    // with m_mutex held
    void updatePeerState()
    {
        if (m_opt.srtMode == SrtMode::Listener)
            m_state = m_peers.empty() ? util::format("listening on port %u", static_cast<unsigned>(m_opt.port))
                                      : util::format("%u SRT client%s", static_cast<unsigned>(m_peers.size()),
                                                     m_peers.size() == 1 ? "" : "s");
        else
            m_state = m_peers.empty() ? "connecting" : "connected";
    }

    void listenerLoop()
    {
        while (true) {
            sockaddr_storage peer;
            int plen = sizeof(peer);
            const SRTSOCKET s = srt_accept(m_listener, reinterpret_cast<sockaddr *>(&peer), &plen);
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) {
                if (s != SRT_INVALID_SOCK)
                    srt_close(s);
                return;
            }
            if (s == SRT_INVALID_SOCK) {
                m_state = std::string("accept failed: ") + srt_getlasterror_str();
                return;
            }
            m_peers.push_back(s);
            updatePeerState();
        }
    }

    void callerLoop(sockaddr_storage addr, int len)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_running) {
            if (!m_peers.empty()) {
                m_cv.wait(lock, [this] { return !m_running || m_peers.empty(); });
                continue;
            }
            const SRTSOCKET s = srt_create_socket();
            std::string err;
            if (s == SRT_INVALID_SOCK || !applyOptions(s, &err)) {
                m_state = err.empty() ? std::string("socket: ") + srt_getlasterror_str() : err;
                if (s != SRT_INVALID_SOCK)
                    srt_close(s);
                m_cv.wait_for(lock, std::chrono::seconds(2), [this] { return !m_running; });
                continue;
            }
            m_connecting = s;
            lock.unlock();
            const int rc = srt_connect(s, reinterpret_cast<sockaddr *>(&addr), len);
            const std::string why = rc == SRT_ERROR ? srt_getlasterror_str() : "";
            lock.lock();
            m_connecting = SRT_INVALID_SOCK;
            if (!m_running) {
                srt_close(s);
                break;
            }
            if (rc == SRT_ERROR) {
                srt_close(s);
                m_state = "connecting (" + why + ")";
                m_cv.wait_for(lock, std::chrono::seconds(1), [this] { return !m_running; });
                continue;
            }
            m_peers.push_back(s);
            updatePeerState();
        }
    }

    TsNetworkOutput m_opt;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::vector<SRTSOCKET> m_peers;
    SRTSOCKET m_listener = SRT_INVALID_SOCK;
    SRTSOCKET m_connecting = SRT_INVALID_SOCK;
    std::string m_state;
    bool m_running = false;
    bool m_started = false;
};
#endif

} // namespace

struct TsNetworkSink::Impl
{
    TsNetworkOutput opt;
    CaptureMode mode;
    std::string url;
    TsFilter filter;
    BbFrameToTs bb2ts;
    int lockedIsi = -2; // -2: no TS input stream seen yet
    std::vector<uint8_t> buf;
    size_t header = 0;
    size_t perMessage = 7;
    bool rtp = false;
    uint16_t rtpSeq = 0;
    uint32_t rtpTsBase = 0;
    uint32_t ssrc = 0;
    std::chrono::steady_clock::time_point t0;
    net::UdpSender udp;
#ifdef TBSDVB_HAVE_SRT
    std::unique_ptr<SrtLink> srt;
#endif
    std::atomic<uint64_t> packets{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<bool> programFound{false};
    std::atomic<bool> pmtSeen{false};
    mutable std::mutex errorMutex;
    std::string lastError;

    Impl(const TsNetworkOutput &o, CaptureMode m)
        : opt(o)
        , mode(m)
        , url(networkUrl(o, true))
        , filter(TsFilterOptions{o.programNumber, o.pids, o.dropNullPackets})
    {
    }

    void pushPacket(const uint8_t *pkt)
    {
        filter.push(pkt, [this](const uint8_t *p) {
            buf.insert(buf.end(), p, p + kTsSize);
            if (buf.size() >= header + perMessage * kTsSize)
                flush();
        });
        if (opt.programNumber > 0) {
            programFound.store(filter.programFound(), std::memory_order_relaxed);
            pmtSeen.store(filter.pmtSeen(), std::memory_order_relaxed);
        }
    }

    void flush()
    {
        if (buf.size() <= header)
            return;
        const size_t n = (buf.size() - header) / kTsSize;
        if (rtp) {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
                                .count();
            const uint32_t ts = rtpTsBase + static_cast<uint32_t>(us * 9 / 100); // 90 kHz
            buf[0] = 0x80;
            buf[1] = 33; // MP2T
            buf[2] = static_cast<uint8_t>(rtpSeq >> 8);
            buf[3] = static_cast<uint8_t>(rtpSeq);
            ++rtpSeq;
            for (int i = 0; i < 4; ++i) {
                buf[4 + i] = static_cast<uint8_t>(ts >> (24 - 8 * i));
                buf[8 + i] = static_cast<uint8_t>(ssrc >> (24 - 8 * i));
            }
        }
        bool ok = true;
#ifdef TBSDVB_HAVE_SRT
        if (srt) {
            const int failed = srt->send(buf.data(), buf.size());
            if (failed > 0) {
                errors += static_cast<uint64_t>(failed);
                ok = false;
            }
            if (srt->peers() == 0)
                ok = false; // nobody to send to; not an error
        } else
#endif
        {
            if (!udp.send(buf.data(), buf.size())) {
                ++errors;
                ok = false;
                std::lock_guard<std::mutex> lock(errorMutex);
                lastError = udp.lastError();
            }
        }
        if (ok) {
            packets += n;
            bytes += n * kTsSize;
        }
        buf.resize(header);
    }
};

TsNetworkSink::TsNetworkSink(const TsNetworkOutput &opt, CaptureMode mode)
    : d(new Impl(opt, mode))
{
}

TsNetworkSink::~TsNetworkSink()
{
    close();
}

bool TsNetworkSink::srtAvailable()
{
#ifdef TBSDVB_HAVE_SRT
    return true;
#else
    return false;
#endif
}

bool TsNetworkSink::open(std::string *error)
{
    close();
    const TsNetworkOutput &o = d->opt;
    d->rtp = o.protocol == NetProtocol::Rtp;
    d->header = d->rtp ? kRtpHeader : 0;
    d->perMessage = static_cast<size_t>(std::min(std::max(o.packetsPerDatagram, 1), 7));
    d->buf.assign(d->header, 0);
    d->buf.reserve(d->header + 7 * kTsSize);
    d->t0 = std::chrono::steady_clock::now();
    std::random_device rd;
    d->ssrc = rd();
    d->rtpSeq = static_cast<uint16_t>(rd());
    d->rtpTsBase = rd();
    d->filter.reset();
    d->bb2ts.reset();
    d->lockedIsi = -2;
    d->packets = d->bytes = d->errors = 0;
    d->programFound = d->pmtSeen = false;

    if (o.protocol == NetProtocol::Srt) {
#ifdef TBSDVB_HAVE_SRT
        d->srt.reset(new SrtLink);
        std::string err;
        if (!d->srt->open(o, &err)) {
            d->srt.reset();
            if (error)
                *error = d->url + ": " + err;
            return false;
        }
        return true;
#else
        if (error)
            *error = "SRT output is not available (tbsdvb was built without libsrt)";
        return false;
#endif
    }
    std::string err;
    if (!d->udp.open(o.host, o.port, o.interfaceName, o.ttl, &err)) {
        if (error)
            *error = d->url + ": " + err;
        return false;
    }
    return true;
}

void TsNetworkSink::close()
{
    if (d->udp.isOpen() && d->buf.size() > d->header)
        d->flush();
    d->udp.close();
#ifdef TBSDVB_HAVE_SRT
    if (d->srt) {
        d->srt->close();
        d->srt.reset();
    }
#endif
}

void TsNetworkSink::writeTs(const uint8_t *packets, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        d->pushPacket(packets + i * kTsSize);
}

void TsNetworkSink::writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr)
{
    if (hdr.streamType() != BbHeader::StreamType::Transport)
        return;
    const int isi = hdr.isi();
    if (isi >= 0) { // multistream: one input stream only
        if (d->opt.isi >= 0 && isi != d->opt.isi)
            return;
        if (d->lockedIsi == -2)
            d->lockedIsi = isi;
        else if (isi != d->lockedIsi)
            return;
    }
    d->bb2ts.pushFrame(frame, len, hdr, [this](const uint8_t *pkt) { d->pushPacket(pkt); });
}

NetOutputStatus TsNetworkSink::status() const
{
    NetOutputStatus s;
    s.url = d->url;
    s.packets = d->packets.load();
    s.bytes = d->bytes.load();
    s.sendErrors = d->errors.load();
    std::string err;
    {
        std::lock_guard<std::mutex> lock(d->errorMutex);
        err = d->lastError;
    }
    std::string transport;
    bool linkUp = d->udp.isOpen();
#ifdef TBSDVB_HAVE_SRT
    if (d->srt) {
        s.peers = d->srt->peers();
        linkUp = s.peers > 0;
        transport = d->srt->state();
    }
#endif
    if (d->opt.programNumber > 0 && !d->programFound.load())
        s.state = util::format("service %d not in the PAT yet", d->opt.programNumber);
    else if (d->opt.programNumber > 0 && !d->pmtSeen.load())
        s.state = util::format("waiting for the PMT of service %d", d->opt.programNumber);
    else if (!transport.empty() && !linkUp)
        s.state = transport;
    else if (s.packets == 0)
        s.state = transport.empty() ? "waiting for data" : transport + ", waiting for data";
    else if (!err.empty() && s.sendErrors > 0)
        s.state = "sending (last error: " + err + ")";
    else
        s.state = transport.empty() ? "sending" : "sending, " + transport;
    s.active = linkUp && s.packets > 0;
    return s;
}

} // namespace tbs
