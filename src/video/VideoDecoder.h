#pragma once

#include "core/ITsConsumer.h"

#include <QImage>
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Decodes the first video stream of the incoming transport stream with FFmpeg
// and emits frames as QImage (on its own thread; connect with a queued connection).
class VideoDecoder : public QObject, public ITsConsumer
{
    Q_OBJECT

public:
    explicit VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder() override;

    void start();
    void stop();

    void consumeTs(const uint8_t *data, size_t len) override;

signals:
    void frameReady(const QImage &image);
    void streamInfo(const QString &info);

private:
    static int readPacket(void *opaque, uint8_t *buf, int size);
    static int interruptCallback(void *opaque);
    int read(uint8_t *buf, int size);
    void run();
    void decodeSession();

    std::thread m_thread;
    std::atomic<bool> m_stop{true};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<uint8_t>> m_queue;
    size_t m_queuedBytes = 0;
    std::vector<uint8_t> m_current;
    size_t m_currentPos = 0;
};
