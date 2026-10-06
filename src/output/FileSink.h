#pragma once

#include "IOutputSink.h"
#include "core/TunerConfig.h"

#include <QFile>

class FileSink : public IOutputSink
{
public:
    explicit FileSink(const TunerConfig &cfg);
    ~FileSink() override;

    bool open(QString *error) override;
    void close() override;
    bool acceptsTs() const override;
    bool acceptsBbFrames() const override;
    void writeTs(const uint8_t *packets, size_t count) override;
    void writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &hdr) override;

    uint64_t bytesWritten() const { return m_bytes; }
    QString fileName() const { return m_file.fileName(); }
    QString lastError() const { return m_error; }

    // Resolves the final file name (adds the timestamp suffix/extension)
    static QString resolvePath(const TunerConfig &cfg);

private:
    void write(const void *data, size_t len);

    TunerConfig m_cfg;
    QFile m_file;
    uint64_t m_bytes = 0;
    QString m_error;
};
