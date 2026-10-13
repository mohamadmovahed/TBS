#include "AudioOutput.h"

#include "VideoDecoder.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QDateTime>
#include <QMediaDevices>
#include <QTimer>
#include <cmath>

namespace {
constexpr qint64 kBytesPerSecond = VideoDecoder::kAudioRate * VideoDecoder::kAudioChannels * 2;
constexpr double kMaxPendingSec = 0.6; // keep the latency low: older audio is dropped
} // namespace

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(10);
    connect(m_timer, &QTimer::timeout, this, &AudioOutput::pump);
}

AudioOutput::~AudioOutput()
{
    stop();
}

bool AudioOutput::available() const
{
    return !QMediaDevices::defaultAudioOutput().isNull();
}

QString AudioOutput::deviceName() const
{
    return QMediaDevices::defaultAudioOutput().description();
}

void AudioOutput::start()
{
    if (m_sink || !available())
        return;
    QAudioFormat f;
    f.setSampleRate(VideoDecoder::kAudioRate);
    f.setChannelCount(VideoDecoder::kAudioChannels);
    f.setSampleFormat(QAudioFormat::Int16);
    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), f, this);
    m_sink->setBufferSize(static_cast<qsizetype>(kBytesPerSecond / 4)); // 250 ms
    applyVolume();
    m_io = m_sink->start();
    reset();
    m_timer->start();
}

void AudioOutput::stop()
{
    m_timer->stop();
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
    }
    m_sink = nullptr;
    m_io = nullptr;
    reset();
}

void AudioOutput::reset()
{
    m_pending.clear();
    m_pendingBytes = 0;
    m_haveClock = false;
    m_smoothValid = false;
}

void AudioOutput::setVolume(double v)
{
    m_volume = v;
    applyVolume();
}

void AudioOutput::setMuted(bool m)
{
    m_muted = m;
    applyVolume();
}

void AudioOutput::applyVolume()
{
    if (m_sink)
        m_sink->setVolume(m_muted ? 0.0 : m_volume);
}

void AudioOutput::push(const QByteArray &pcm, double pts)
{
    if (!m_io)
        return;
    if (std::isnan(pts)) {
        // Unknown timestamp: continue from the previous chunk
        pts = m_pending.empty() ? m_writtenEnd : m_pending.back().second
                                                     + double(m_pending.back().first.size()) / kBytesPerSecond;
    }
    m_pending.emplace_back(pcm, pts);
    m_pendingBytes += pcm.size();
    while (m_pendingBytes > kMaxPendingSec * kBytesPerSecond && m_pending.size() > 1) {
        m_pendingBytes -= m_pending.front().first.size();
        m_pending.pop_front();
    }
    pump();
}

void AudioOutput::pump()
{
    if (!m_io || !m_sink)
        return;
    qint64 free = m_sink->bytesFree();
    while (!m_pending.empty() && free >= 4) {
        auto &front = m_pending.front();
        const qint64 n = std::min<qint64>(free & ~qint64(3), front.first.size());
        const qint64 w = m_io->write(front.first.constData(), n);
        if (w <= 0)
            break;
        m_writtenEnd = front.second + double(w) / kBytesPerSecond;
        m_haveClock = true;
        m_lastWriteMs = QDateTime::currentMSecsSinceEpoch();
        free -= w;
        m_pendingBytes -= w;
        if (w == front.first.size()) {
            m_pending.pop_front();
        } else {
            front.first.remove(0, static_cast<int>(w));
            front.second += double(w) / kBytesPerSecond;
        }
    }
}

bool AudioOutput::clock(double *pts) const
{
    if (!m_sink || !m_haveClock)
        return false;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // Nothing written for a while: the audio stopped (radio gap, service change)
    if (nowMs - m_lastWriteMs > 1000)
        return false;
    const qint64 buffered = std::max<qint64>(0, m_sink->bufferSize() - m_sink->bytesFree());
    const double raw = m_writtenEnd - double(buffered) / kBytesPerSecond;
    // The device position moves in period-sized steps; run the clock on wall time between
    // them and pull it gently towards the device position
    const double wall = nowMs / 1000.0;
    double est = m_smoothValid ? m_smoothPts + (wall - m_smoothWall) : raw;
    if (!m_smoothValid || std::fabs(raw - est) > 0.15)
        est = raw;
    else
        est += (raw - est) * 0.02;
    m_smoothPts = est;
    m_smoothWall = wall;
    m_smoothValid = true;
    *pts = est;
    return true;
}
