#include "AvSync.h"

#include "VideoWidget.h"

#include <QTimer>
#include <cmath>

namespace {
constexpr size_t kMaxQueued = 60;      // ~2 s at 30 frames/s
constexpr double kResync = 3.0;        // larger offsets: clocks disagree (PTS jump), show now
} // namespace

AvSync::AvSync(VideoWidget *video, Clock audioClock, QObject *parent)
    : QObject(parent)
    , m_video(video)
    , m_clock(std::move(audioClock))
{
    m_timer = new QTimer(this);
    m_timer->setInterval(8);
    connect(m_timer, &QTimer::timeout, this, &AvSync::tick);
    m_timer->start();
}

void AvSync::reset()
{
    m_queue.clear();
}

void AvSync::push(const QImage &frame, double pts)
{
    double now = 0;
    if (std::isnan(pts) || !m_clock || !m_clock(&now)) {
        // No audio clock: show at once (live source, arrives in real time)
        m_queue.clear();
        m_video->setFrame(frame);
        return;
    }
    m_queue.emplace_back(frame, pts);
    if (m_queue.size() > kMaxQueued)
        m_queue.pop_front();
    tick();
}

void AvSync::tick()
{
    if (m_queue.empty())
        return;
    double now = 0;
    if (!m_clock || !m_clock(&now)) {
        m_video->setFrame(m_queue.back().first);
        m_queue.clear();
        return;
    }
    // Clocks far apart: resynchronise on the newest frame
    if (std::fabs(m_queue.front().second - now) > kResync) {
        m_video->setFrame(m_queue.back().first);
        m_queue.clear();
        return;
    }
    // Show the newest frame that is due; earlier due frames are late and skipped
    while (m_queue.size() > 1 && m_queue[1].second <= now + 0.01)
        m_queue.pop_front();
    if (m_queue.front().second <= now + 0.01) {
        m_video->setFrame(m_queue.front().first);
        m_queue.pop_front();
    }
}
