#include "FileSink.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

FileSink::FileSink(const TunerConfig &cfg)
    : m_cfg(cfg)
{
}

FileSink::~FileSink()
{
    close();
}

QString FileSink::resolvePath(const TunerConfig &cfg)
{
    QString path = cfg.filePath;
    const bool bb = cfg.captureMode == CaptureMode::BBFrame && cfg.bbFileFormat != BbFileFormat::PseudoTs;
    const QString defaultExt = bb ? QStringLiteral("bbf") : QStringLiteral("ts");
    if (path.isEmpty())
        path = QDir::home().filePath(QStringLiteral("capture.") + defaultExt);

    QFileInfo fi(path);
    QString ext = fi.suffix();
    if (ext.isEmpty())
        ext = defaultExt;
    QString base = fi.completeBaseName();
    if (cfg.fileTimestampSuffix)
        base += QDateTime::currentDateTime().toString(QStringLiteral("_yyyyMMdd_HHmmss"));
    return fi.dir().filePath(base + QLatin1Char('.') + ext);
}

bool FileSink::open(QString *error)
{
    const QString path = resolvePath(m_cfg);
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Cannot create '%1': %2").arg(path, m_file.errorString());
        return false;
    }
    m_bytes = 0;
    return true;
}

void FileSink::close()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

bool FileSink::acceptsTs() const
{
    return m_cfg.captureMode == CaptureMode::TransportStream || m_cfg.bbFileFormat == BbFileFormat::PseudoTs;
}

bool FileSink::acceptsBbFrames() const
{
    return m_cfg.captureMode == CaptureMode::BBFrame && m_cfg.bbFileFormat != BbFileFormat::PseudoTs;
}

void FileSink::write(const void *data, size_t len)
{
    if (!m_file.isOpen())
        return;
    const qint64 n = m_file.write(static_cast<const char *>(data), static_cast<qint64>(len));
    if (n < 0) {
        m_error = m_file.errorString();
        m_file.close();
        return;
    }
    m_bytes += static_cast<uint64_t>(n);
}

void FileSink::writeTs(const uint8_t *packets, size_t count)
{
    write(packets, count * 188);
}

void FileSink::writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &)
{
    if (m_cfg.bbFileFormat == BbFileFormat::B8Marker) {
        const uint8_t marker = 0xB8;
        write(&marker, 1);
    }
    write(frame, len);
}
