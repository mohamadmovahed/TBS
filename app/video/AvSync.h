#pragma once

#include <QImage>
#include <QObject>
#include <deque>
#include <functional>
#include <utility>

class QTimer;
class VideoWidget;

// Shows decoded video frames on time: against the audio clock when audio is playing,
// otherwise as they arrive. GUI thread only.
class AvSync : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<bool(double *pts)>;

    AvSync(VideoWidget *video, Clock audioClock, QObject *parent = nullptr);

public slots:
    void push(const QImage &frame, double pts);
    void reset();

private:
    void tick();

    VideoWidget *m_video;
    Clock m_clock;
    QTimer *m_timer;
    std::deque<std::pair<QImage, double>> m_queue;
};
