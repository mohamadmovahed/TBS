#pragma once

#include <QByteArray>
#include <QObject>
#include <deque>
#include <utility>

class QAudioSink;
class QIODevice;
class QTimer;

// Plays the preview audio (48 kHz stereo int16 from VideoDecoder) on the default output
// device and provides the playback position as the clock for the video frames.
// GUI thread only.
class AudioOutput : public QObject
{
    Q_OBJECT

public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput() override;

    bool available() const;   // an output device exists
    QString deviceName() const;

    void start();
    void stop();
    void reset();             // drops queued audio (new decoding session)
    void setVolume(double v); // 0..1
    void setMuted(bool m);

    // Position of the sample now being heard, in stream time (seconds). False when no audio is playing.
    bool clock(double *pts) const;

public slots:
    void push(const QByteArray &pcm, double pts);

private:
    void pump();
    void applyVolume();

    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr;
    QTimer *m_timer = nullptr;
    std::deque<std::pair<QByteArray, double>> m_pending;
    qint64 m_pendingBytes = 0;
    double m_writtenEnd = 0.0; // stream time just after the last byte handed to the device
    bool m_haveClock = false;
    qint64 m_lastWriteMs = 0;
    mutable double m_smoothPts = 0.0;  // smoothed clock (see clock())
    mutable double m_smoothWall = 0.0;
    mutable bool m_smoothValid = false;
    double m_volume = 0.8;
    bool m_muted = false;
};
