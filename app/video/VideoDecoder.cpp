#include "VideoDecoder.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef TBS_HAVE_AUDIO
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#endif
}

namespace {
constexpr size_t kMaxQueuedBytes = 16u << 20;
constexpr int kIoBufferSize = 188 * 256;
constexpr int kMaxPreviewWidth = 1280;
constexpr qint64 kMinFrameIntervalMs = 33;

double toSeconds(int64_t ts, AVRational tb)
{
    return ts == AV_NOPTS_VALUE ? std::nan("") : ts * av_q2d(tb);
}

QString codecName(const AVCodecParameters *par)
{
    const AVCodecDescriptor *d = avcodec_descriptor_get(par->codec_id);
    return QString::fromLatin1(d ? d->name : avcodec_get_name(par->codec_id)).toUpper();
}

#ifdef TBS_HAVE_AUDIO
// Converts decoded audio to 48 kHz stereo int16
class Resampler
{
public:
    ~Resampler()
    {
        if (m_swr)
            swr_free(&m_swr);
    }

    bool convert(const AVFrame *f, QByteArray *out)
    {
        if (!m_swr || f->format != m_inFormat || f->sample_rate != m_inRate || channels(f) != m_inChannels) {
            if (m_swr)
                swr_free(&m_swr);
#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 5, 100)
            AVChannelLayout outLayout;
            av_channel_layout_default(&outLayout, VideoDecoder::kAudioChannels);
            AVChannelLayout inLayout;
            if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || f->ch_layout.nb_channels == 0)
                av_channel_layout_default(&inLayout, std::max(1, f->ch_layout.nb_channels));
            else
                av_channel_layout_copy(&inLayout, &f->ch_layout);
            const int rc = swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_S16, VideoDecoder::kAudioRate,
                                               &inLayout, static_cast<AVSampleFormat>(f->format), f->sample_rate, 0,
                                               nullptr);
            av_channel_layout_uninit(&inLayout);
            av_channel_layout_uninit(&outLayout);
            if (rc < 0 || !m_swr || swr_init(m_swr) < 0)
                return false;
#else
            const int64_t inLayout = f->channel_layout ? static_cast<int64_t>(f->channel_layout)
                                                       : av_get_default_channel_layout(f->channels);
            m_swr = swr_alloc_set_opts(nullptr, av_get_default_channel_layout(VideoDecoder::kAudioChannels),
                                       AV_SAMPLE_FMT_S16, VideoDecoder::kAudioRate, inLayout,
                                       static_cast<AVSampleFormat>(f->format), f->sample_rate, 0, nullptr);
            if (!m_swr || swr_init(m_swr) < 0)
                return false;
#endif
            m_inFormat = f->format;
            m_inRate = f->sample_rate;
            m_inChannels = channels(f);
        }
        const int maxOut = static_cast<int>(
            av_rescale_rnd(swr_get_delay(m_swr, f->sample_rate) + f->nb_samples, VideoDecoder::kAudioRate,
                           f->sample_rate, AV_ROUND_UP));
        out->resize(maxOut * VideoDecoder::kAudioChannels * 2);
        uint8_t *dst = reinterpret_cast<uint8_t *>(out->data());
        const int n = swr_convert(m_swr, &dst, maxOut, const_cast<const uint8_t **>(f->extended_data), f->nb_samples);
        if (n < 0)
            return false;
        out->resize(n * VideoDecoder::kAudioChannels * 2);
        return true;
    }

private:
    static int channels(const AVFrame *f)
    {
#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 5, 100)
        return f->ch_layout.nb_channels;
#else
        return f->channels;
#endif
    }

    SwrContext *m_swr = nullptr;
    int m_inFormat = -1;
    int m_inRate = 0;
    int m_inChannels = 0;
};
#endif
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
        if (m_filter)
            m_filter->reset();
    }
    m_stop = false;
    m_restart = false;
    m_thread = std::thread(&VideoDecoder::run, this);
}

void VideoDecoder::stop()
{
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void VideoDecoder::setSelection(int programNumber, int audioPid)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (programNumber == m_program && audioPid == m_audioPid)
            return;
        const bool newService = programNumber != m_program;
        m_program = programNumber;
        m_audioPid = audioPid;
        if (!newService) {
            m_audioSwitch = true; // the running session reopens the audio decoder
            return;
        }
        {
            m_filter.reset();
            if (programNumber > 0) {
                tbs::TsFilterOptions o;
                o.programNumber = programNumber;
                m_filter = std::make_unique<tbs::TsFilter>(o);
            }
            // Data of the old service is useless now
            m_queue.clear();
            m_queuedBytes = 0;
            m_current.clear();
            m_currentPos = 0;
        }
    }
    m_restart = true;
    m_cv.notify_all();
}

void VideoDecoder::setAudioWanted(bool on)
{
    if (m_audioWanted.exchange(on) != on) {
        m_restart = true;
        m_cv.notify_all();
    }
}

void VideoDecoder::consumeTs(const uint8_t *data, size_t len)
{
    if (m_stop.load())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint8_t *p = data;
        size_t n = len;
        if (m_filter) {
            m_filtered.clear();
            for (size_t i = 0; i + 188 <= len; i += 188)
                m_filter->push(data + i, [this](const uint8_t *pkt) { m_filtered.insert(m_filtered.end(), pkt, pkt + 188); });
            if (m_filtered.empty())
                return;
            p = m_filtered.data();
            n = m_filtered.size();
        }
        if (m_queuedBytes + n > kMaxQueuedBytes)
            return; // decoder is behind; drop rather than block the capture thread
        m_queue.emplace_back(p, p + n);
        m_queuedBytes += n;
    }
    m_cv.notify_one();
}

int VideoDecoder::readPacket(void *opaque, uint8_t *buf, int size)
{
    return static_cast<VideoDecoder *>(opaque)->read(buf, size);
}

int VideoDecoder::interruptCallback(void *opaque)
{
    return static_cast<VideoDecoder *>(opaque)->interrupted() ? 1 : 0;
}

int VideoDecoder::read(uint8_t *buf, int size)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_currentPos >= m_current.size()) {
        if (interrupted())
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
        m_restart = false;
        emit sessionStarted();
        decodeSession();
        if (!m_stop.load() && !m_restart.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void VideoDecoder::decodeSession()
{
    auto *ioBuffer = static_cast<uint8_t *>(av_malloc(kIoBufferSize));
    AVIOContext *avio = avio_alloc_context(ioBuffer, kIoBufferSize, 0, this, &VideoDecoder::readPacket, nullptr, nullptr);
    AVFormatContext *fmt = avformat_alloc_context();
    AVCodecContext *codecCtx = nullptr;
    AVCodecContext *audioCtx = nullptr;
    SwsContext *sws = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    auto cleanup = [&]() {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        if (sws)
            sws_freeContext(sws);
        avcodec_free_context(&codecCtx);
        avcodec_free_context(&audioCtx);
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
    av_dict_set(&opts, "probesize", "4000000", 0);
    av_dict_set(&opts, "analyzeduration", "2000000", 0);
    auto inputFormat = av_find_input_format("mpegts");
    if (avformat_open_input(&fmt, nullptr, inputFormat, &opts) < 0) {
        av_dict_free(&opts);
        fmt = nullptr; // freed by avformat_open_input on failure
        cleanup();
        return;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(fmt, nullptr) < 0 || interrupted()) {
        cleanup();
        return;
    }

    const int videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audioStream = -1;
    QString videoInfo;
    QString audioInfo;
    bool sizeKnown = false;
    auto emitInfo = [&]() {
        QStringList parts;
        if (videoStream < 0)
            parts << QStringLiteral("Radio (no video)");
        else
            parts << videoInfo;
        if (!audioInfo.isEmpty())
            parts << audioInfo;
        emit streamInfo(parts.join(QStringLiteral(" · ")));
    };
    auto updateDiscard = [&]() {
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            fmt->streams[i]->discard = static_cast<int>(i) == videoStream || static_cast<int>(i) == audioStream
                ? AVDISCARD_DEFAULT
                : AVDISCARD_ALL;
    };

#ifdef TBS_HAVE_AUDIO
    std::unique_ptr<Resampler> resampler;
    // (Re)opens the audio decoder for a PID (-1 default, 0 none); used at start and when
    // only the audio track changes, so the video keeps running
    auto openAudio = [&](int pid) {
        avcodec_free_context(&audioCtx);
        resampler.reset();
        audioStream = -1;
        audioInfo.clear();
        if (pid == 0 || !m_audioWanted.load())
            return;
        if (pid > 0)
            for (unsigned i = 0; i < fmt->nb_streams; ++i)
                if (fmt->streams[i]->id == pid && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    audioStream = static_cast<int>(i);
        if (audioStream < 0)
            audioStream = std::max(-1, av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, videoStream, nullptr, 0));
        if (audioStream < 0)
            return;
        AVCodecParameters *par = fmt->streams[audioStream]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (codec) {
            audioCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(audioCtx, par);
            if (avcodec_open2(audioCtx, codec, nullptr) < 0)
                avcodec_free_context(&audioCtx);
        }
        if (!audioCtx) {
            audioInfo = QStringLiteral("no decoder for audio %1").arg(codecName(par));
            audioStream = -1;
            return;
        }
        resampler = std::make_unique<Resampler>();
        const AVDictionaryEntry *lang = av_dict_get(fmt->streams[audioStream]->metadata, "language", nullptr, 0);
        audioInfo = QStringLiteral("%1 %2 kHz%3, PID 0x%4")
                        .arg(codecName(par))
                        .arg(par->sample_rate / 1000.0, 0, 'g', 3)
                        .arg(lang ? QStringLiteral(" ") + QString::fromLatin1(lang->value) : QString())
                        .arg(fmt->streams[audioStream]->id, 4, 16, QLatin1Char('0'));
    };
    int audioPid = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        audioPid = m_audioPid;
        m_audioSwitch = false;
    }
    openAudio(audioPid);
#endif
    if (videoStream < 0 && audioStream < 0) {
        emit streamInfo(QStringLiteral("No video or audio stream found"));
        cleanup();
        return;
    }
    updateDiscard();

    if (videoStream >= 0) {
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
        sizeKnown = par->width > 0 && par->height > 0;
        videoInfo = sizeKnown ? QStringLiteral("%1, %2x%3, PID 0x%4").arg(codecName(par)).arg(par->width).arg(par->height)
                                    .arg(fmt->streams[videoStream]->id, 4, 16, QLatin1Char('0'))
                              : QStringLiteral("%1, PID 0x%2").arg(codecName(par))
                                    .arg(fmt->streams[videoStream]->id, 4, 16, QLatin1Char('0'));
    }
    emitInfo();

    QElapsedTimer sinceLastFrame;
    sinceLastFrame.start();
    bool firstFrame = true;
    double lastPts = std::nan("");
#ifdef TBS_HAVE_AUDIO
    QByteArray pcm;
#endif

    while (!interrupted()) {
#ifdef TBS_HAVE_AUDIO
        if (m_audioSwitch.exchange(false)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                audioPid = m_audioPid;
            }
            openAudio(audioPid);
            updateDiscard();
            emitInfo();
            emit audioRestarted();
        }
#endif
        const int rr = av_read_frame(fmt, pkt);
        if (rr < 0) {
            if (rr == AVERROR(EAGAIN))
                continue;
            break;
        }
#ifdef TBS_HAVE_AUDIO
        if (pkt->stream_index == audioStream && audioCtx) {
            const AVRational tb = fmt->streams[audioStream]->time_base;
            if (avcodec_send_packet(audioCtx, pkt) >= 0) {
                while (avcodec_receive_frame(audioCtx, frame) == 0) {
                    const double pts = toSeconds(frame->best_effort_timestamp, tb);
                    if (resampler->convert(frame, &pcm) && !pcm.isEmpty())
                        emit audioReady(pcm, pts);
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(pkt);
            continue;
        }
#endif
        if (pkt->stream_index != videoStream || !codecCtx) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(codecCtx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        const AVRational tb = fmt->streams[videoStream]->time_base;
        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            if (!sizeKnown) {
                sizeKnown = true;
                videoInfo = QStringLiteral("%1, %2x%3, PID 0x%4")
                                .arg(codecName(fmt->streams[videoStream]->codecpar))
                                .arg(frame->width)
                                .arg(frame->height)
                                .arg(fmt->streams[videoStream]->id, 4, 16, QLatin1Char('0'));
                emitInfo();
            }
            // At most ~30 frames/s: by PTS spacing (even with decoding bursts), else by wall time
            const double pts = toSeconds(frame->best_effort_timestamp, tb);
            const bool havePts = !std::isnan(pts) && !std::isnan(lastPts);
            const bool tooSoon = havePts ? (pts > lastPts && pts - lastPts < kMinFrameIntervalMs / 1000.0)
                                         : sinceLastFrame.elapsed() < kMinFrameIntervalMs;
            if (!firstFrame && tooSoon) {
                av_frame_unref(frame);
                continue;
            }
            firstFrame = false;
            lastPts = pts;
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
                emit frameReady(img, pts);
            }
            av_frame_unref(frame);
        }
    }
    cleanup();
}
