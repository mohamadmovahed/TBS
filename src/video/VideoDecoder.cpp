#include "VideoDecoder.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr size_t kMaxQueuedBytes = 16u << 20;
constexpr int kIoBufferSize = 188 * 256;
constexpr int kMaxPreviewWidth = 1280;
constexpr qint64 kMinFrameIntervalMs = 33;
} // namespace

VideoDecoder::VideoDecoder(QObject *parent)
    : QObject(parent)
{
}

VideoDecoder::~VideoDecoder()
{
    stop();
}

void VideoDecoder::start()
{
    if (!m_stop.load())
        return;
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_queuedBytes = 0;
        m_current.clear();
        m_currentPos = 0;
    }
    m_stop = false;
    m_thread = std::thread(&VideoDecoder::run, this);
}

void VideoDecoder::stop()
{
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void VideoDecoder::consumeTs(const uint8_t *data, size_t len)
{
    if (m_stop.load())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queuedBytes + len > kMaxQueuedBytes)
            return; // decoder is behind; drop rather than block the capture thread
        m_queue.emplace_back(data, data + len);
        m_queuedBytes += len;
    }
    m_cv.notify_one();
}

int VideoDecoder::readPacket(void *opaque, uint8_t *buf, int size)
{
    return static_cast<VideoDecoder *>(opaque)->read(buf, size);
}

int VideoDecoder::interruptCallback(void *opaque)
{
    return static_cast<VideoDecoder *>(opaque)->m_stop.load() ? 1 : 0;
}

int VideoDecoder::read(uint8_t *buf, int size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_currentPos >= m_current.size()) {
        if (m_stop.load())
            return AVERROR_EOF;
        if (!m_queue.empty()) {
            m_current = std::move(m_queue.front());
            m_queue.pop_front();
            m_queuedBytes -= m_current.size();
            m_currentPos = 0;
            break;
        }
        m_cv.wait_for(lock, std::chrono::milliseconds(200));
    }
    const size_t n = std::min(static_cast<size_t>(size), m_current.size() - m_currentPos);
    std::memcpy(buf, m_current.data() + m_currentPos, n);
    m_currentPos += n;
    return static_cast<int>(n);
}

void VideoDecoder::run()
{
    while (!m_stop.load()) {
        decodeSession();
        if (!m_stop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void VideoDecoder::decodeSession()
{
    auto *ioBuffer = static_cast<uint8_t *>(av_malloc(kIoBufferSize));
    AVIOContext *avio = avio_alloc_context(ioBuffer, kIoBufferSize, 0, this, &VideoDecoder::readPacket, nullptr, nullptr);
    AVFormatContext *fmt = avformat_alloc_context();
    AVCodecContext *codecCtx = nullptr;
    SwsContext *sws = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    auto cleanup = [&]() {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        if (sws)
            sws_freeContext(sws);
        avcodec_free_context(&codecCtx);
        if (fmt)
            avformat_close_input(&fmt);
        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
    };

    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt->interrupt_callback.callback = &VideoDecoder::interruptCallback;
    fmt->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "8000000", 0);
    av_dict_set(&opts, "analyzeduration", "4000000", 0);
    auto inputFormat = av_find_input_format("mpegts");
    if (avformat_open_input(&fmt, nullptr, inputFormat, &opts) < 0) {
        av_dict_free(&opts);
        fmt = nullptr; // freed by avformat_open_input on failure
        cleanup();
        return;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        cleanup();
        return;
    }

    const int videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream < 0) {
        emit streamInfo(QStringLiteral("No video stream found"));
        cleanup();
        return;
    }
    AVCodecParameters *par = fmt->streams[videoStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        emit streamInfo(QStringLiteral("No decoder for codec %1").arg(QString::fromLatin1(avcodec_get_name(par->codec_id))));
        cleanup();
        return;
    }
    codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, par);
    codecCtx->thread_count = 0;
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        emit streamInfo(QStringLiteral("Cannot open %1 decoder").arg(QString::fromLatin1(codec->name)));
        cleanup();
        return;
    }
    emit streamInfo(QStringLiteral("%1, %2x%3, PID 0x%4")
                        .arg(QString::fromLatin1(codec->long_name ? codec->long_name : codec->name))
                        .arg(par->width)
                        .arg(par->height)
                        .arg(fmt->streams[videoStream]->id, 4, 16, QLatin1Char('0')));

    QElapsedTimer sinceLastFrame;
    sinceLastFrame.start();
    bool firstFrame = true;

    while (!m_stop.load()) {
        const int rr = av_read_frame(fmt, pkt);
        if (rr < 0) {
            if (rr == AVERROR(EAGAIN))
                continue;
            break;
        }
        if (pkt->stream_index != videoStream) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(codecCtx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            if (!firstFrame && sinceLastFrame.elapsed() < kMinFrameIntervalMs) {
                av_frame_unref(frame);
                continue;
            }
            firstFrame = false;
            sinceLastFrame.restart();

            const int srcW = frame->width;
            const int srcH = frame->height;
            double dar = static_cast<double>(srcW) / std::max(1, srcH);
            if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
                dar *= av_q2d(frame->sample_aspect_ratio);
            int dstH = std::min(srcH, 720);
            int dstW = static_cast<int>(dstH * dar + 0.5) & ~1;
            if (dstW > kMaxPreviewWidth) {
                dstW = kMaxPreviewWidth;
                dstH = static_cast<int>(dstW / dar + 0.5) & ~1;
            }
            if (dstW <= 0 || dstH <= 0) {
                av_frame_unref(frame);
                continue;
            }

            sws = sws_getCachedContext(sws, srcW, srcH, static_cast<AVPixelFormat>(frame->format), dstW, dstH,
                                       AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
                QImage img(dstW, dstH, QImage::Format_RGB32);
                uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
                sws_scale(sws, frame->data, frame->linesize, 0, srcH, dst, dstStride);
                emit frameReady(img);
            }
            av_frame_unref(frame);
        }
    }
    cleanup();
}
