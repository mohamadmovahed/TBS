#pragma once

#include <tbsdvb/TsFilter.h>

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Decodes the preview with FFmpeg on its own thread (connect with queued connections):
// the video and one audio stream of the selected service, or of the first service with
// video. Frames carry their presentation time so the GUI can keep them in sync with audio.
class VideoDecoder : public QObject
{
    Q_OBJECT

public:
    // Audio is converted to this format
    static constexpr int kAudioRate = 48000;
    static constexpr int kAudioChannels = 2; // signed 16-bit interleaved

    explicit VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder() override;

    void start();
    void stop();

    // Service (0 = first service with video, whole TS) and audio PID (-1 = default track of
    // the service, 0 = no audio). Thread safe; restarts decoding when it changes.
    void setSelection(int programNumber, int audioPid);
    // Whether audio is decoded at all (no audio device: skip the work)
    void setAudioWanted(bool on);

    // Thread safe; drops data when the decoder falls behind
    void consumeTs(const uint8_t *data, size_t len);

signals:
    // pts in seconds (NaN if unknown)
    void frameReady(const QImage &image, double pts);
    // kAudioRate Hz, kAudioChannels x int16, pts of the first sample in seconds
    void audioReady(const QByteArray &pcm, double pts);
    void streamInfo(const QString &info);
    // A new decoding session started (selection changed, stream restarted): drop queued media
    void sessionStarted();
    // Only the audio track changed: drop queued audio
    void audioRestarted();

private:
    static int readPacket(void *opaque, uint8_t *buf, int size);
    static int interruptCallback(void *opaque);
    int read(uint8_t *buf, int size);
    void run();
    void decodeSession();
    bool interrupted() const { return m_stop.load() || m_restart.load(); }

    std::thread m_thread;
    std::atomic<bool> m_stop{true};
    std::atomic<bool> m_restart{false};
    std::atomic<bool> m_audioWanted{true};
    std::atomic<bool> m_audioSwitch{false};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<uint8_t>> m_queue;
    size_t m_queuedBytes = 0;
    std::vector<uint8_t> m_current;
    size_t m_currentPos = 0;
    // Selection (under m_mutex)
    int m_program = 0;
    int m_audioPid = -1;
    std::unique_ptr<tbs::TsFilter> m_filter;
    std::vector<uint8_t> m_filtered;
};
