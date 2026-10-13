#include "OverviewPanel.h"

#include "../Theme.h"

#include <tbsdvb/DvbNames.h>

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

using namespace tbs;

namespace {
QString count(uint64_t v)
{
    return QLocale().toString(static_cast<qulonglong>(v));
}
// Error counters: zero in normal ink, non-zero flagged with a label (not colour alone)
QString errors(uint64_t v)
{
    if (!v)
        return QStringLiteral("0");
    return QStringLiteral("<span style='color:%1'>●</span> %2").arg(Theme::tokens().critical.name(), count(v));
}
QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}
} // namespace

OverviewPanel::OverviewPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 12, 0, 0);
    grid->setSpacing(12);

    QGridLayout *g = nullptr;
    QFrame *demod = makeCard(tr("Demodulator"), g);
    m_delsys = addRow(g, tr("System"));
    m_modcod = addRow(g, tr("MODCOD"));
    m_rolloff = addRow(g, tr("Roll-off / pilots"));
    m_freq = addRow(g, tr("Frequency (IF)"));
    m_sr = addRow(g, tr("Symbol rate"));
    m_streamId = addRow(g, tr("Stream ID"));
    m_ucb = addRow(g, tr("Error blocks"));
    m_temp = addRow(g, tr("Chip temperature"));

    QFrame *transport = makeCard(tr("Transport"), g);
    m_bytes = addRow(g, tr("Received"));
    m_packets = addRow(g, tr("TS packets"));
    m_cc = addRow(g, tr("CC errors"));
    m_tei = addRow(g, tr("TEI errors"));
    m_sync = addRow(g, tr("Sync losses"));
    m_overflows = addRow(g, tr("Driver overflows"));
    m_pipeline = addRow(g, tr("Processing buffer"));

    m_bbCard = makeCard(tr("Baseband frames"), g);
    m_bbContent = addRow(g, tr("Content"));
    m_bbContent->setWordWrap(true);
    m_bbFrames = addRow(g, tr("Frames"));
    m_bbRate = addRow(g, tr("Rate"));
    m_bbErrors = addRow(g, tr("CRC / seq / length"));
    m_bbType = addRow(g, tr("Stream type"));
    m_bbFlags = addRow(g, tr("MATYPE"));
    m_bbLengths = addRow(g, tr("UPL / DFL / SYNCD"));

    QFrame *outputs = makeCard(tr("Outputs"), g);
    m_file = addRow(g, tr("File"));
    m_file->setWordWrap(true);
    m_zmq = addRow(g, tr("ZeroMQ"));

    grid->addWidget(demod, 0, 0);
    grid->addWidget(transport, 0, 1);
    grid->addWidget(m_bbCard, 1, 0);
    grid->addWidget(outputs, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(2, 1);
    reset();
}

QFrame *OverviewPanel::makeCard(const QString &title, QGridLayout *&grid)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("Card"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 6, 16, 14);
    v->addWidget(Theme::sectionTitle(title));
    grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    v->addLayout(grid);
    v->addStretch();
    return card;
}

QLabel *OverviewPanel::addRow(QGridLayout *grid, const QString &key)
{
    const int row = grid->rowCount();
    auto *k = new QLabel(key);
    k->setObjectName(QStringLiteral("KeyLabel"));
    auto *v = new QLabel;
    v->setObjectName(QStringLiteral("ValueLabel"));
    v->setTextFormat(Qt::RichText);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(k, row, 0, Qt::AlignTop);
    grid->addWidget(v, row, 1, Qt::AlignTop);
    return v;
}

void OverviewPanel::setBbFrameMode(bool on)
{
    m_bbCard->setVisible(on);
}

void OverviewPanel::reset()
{
    for (QLabel *l : findChildren<QLabel *>(QStringLiteral("ValueLabel")))
        l->setText(QStringLiteral("–"));
}

void OverviewPanel::updateContent(const tbs::bb::ProcessorSnapshot &snap)
{
    QStringList parts;
    for (const auto &s : snap.streams) {
        if (s.content == tbs::bb::StreamContent::TsInput)
            continue;
        QString what = QString::fromLatin1(tbs::bb::streamContentName(s.content));
        if (s.mpe.datagrams)
            what += tr(" + MPE (IP)");
        parts << (s.isi < 0 ? QStringLiteral("<b>%1</b>").arg(what) : QStringLiteral("ISI %1: <b>%2</b>").arg(s.isi).arg(what));
    }
    if (!parts.isEmpty())
        m_bbContent->setText(parts.join(QStringLiteral("<br>")));
}

void OverviewPanel::updateFrontend(const FrontendStatus &st)
{
    m_ucb->setText(st.hasErrorBlocks ? errors(st.errorBlocks) : QStringLiteral("–"));
    m_temp->setText(st.hasTemperature ? QStringLiteral("%1 °C").arg(st.temperatureC, 0, 'f', 0) : QStringLiteral("–"));
    if (st.locked() && st.deliverySystem >= 0) {
        m_delsys->setText(qs(DvbNames::deliverySystem(st.deliverySystem)));
        m_modcod->setText(QStringLiteral("<b>%1 %2</b>").arg(qs(DvbNames::modulation(st.modulation)), qs(DvbNames::fec(st.fec))));
        m_rolloff->setText(QStringLiteral("%1 / %2").arg(qs(DvbNames::rolloff(st.rolloff)), qs(DvbNames::pilot(st.pilot))));
        m_freq->setText(QStringLiteral("%1 MHz").arg(st.frequencyKHz / 1000.0, 0, 'f', 3));
        m_sr->setText(QStringLiteral("%1 kS/s").arg(st.symbolRate / 1000.0, 0, 'f', 3));
        m_streamId->setText(st.streamId < 0 ? tr("none")
                                            : QStringLiteral("ISI %1 · 0x%2")
                                                  .arg(st.streamId & 0xFF)
                                                  .arg(static_cast<qulonglong>(st.streamId), 8, 16, QLatin1Char('0')));
    } else if (!st.locked()) {
        for (QLabel *l : {m_delsys, m_modcod, m_rolloff, m_freq, m_sr, m_streamId})
            l->setText(QStringLiteral("–"));
    }
}

void OverviewPanel::updateStats(const StreamStats &st)
{
    m_bytes->setText(QLocale().formattedDataSize(static_cast<qint64>(st.bytesIn)));
    m_packets->setText(count(st.tsPackets));
    m_cc->setText(errors(st.ccErrors));
    m_tei->setText(errors(st.teiErrors));
    m_sync->setText(errors(st.syncLosses));
    m_overflows->setText(errors(st.driverOverflows));
    if (st.pipelineCapacityBytes) {
        const double fill = 100.0 * st.pipelineQueuedBytes / st.pipelineCapacityBytes;
        const double peak = 100.0 * st.pipelinePeakBytes / st.pipelineCapacityBytes;
        QString text = tr("%1 % · peak %2 % of %3").arg(fill, 0, 'f', 0).arg(peak, 0, 'f', 0)
                           .arg(QLocale().formattedDataSize(static_cast<qint64>(st.pipelineCapacityBytes)));
        if (st.pipelineDroppedBytes)
            text += tr(" · dropped %1").arg(QStringLiteral("<span style='color:%1'>● </span>%2")
                                                  .arg(Theme::tokens().critical.name(),
                                                       QLocale().formattedDataSize(static_cast<qint64>(st.pipelineDroppedBytes))));
        m_pipeline->setText(text);
    }

    m_bbFrames->setText(count(st.bbFrames));
    m_bbRate->setText(tr("%1 frames/s · %2 Mbit/s").arg(st.bbFramesPerSec, 0, 'f', 1).arg(st.bbPayloadMbps, 0, 'f', 2));
    m_bbErrors->setText(QStringLiteral("%1 / %2 / %3").arg(errors(st.bbCrcErrors), errors(st.bbSequenceErrors), errors(st.bbLengthErrors)));
    if (st.hasBbHeader) {
        const BbHeader &h = st.lastBbHeader;
        m_bbType->setText(QStringLiteral("%1 · %2").arg(qs(h.streamTypeName()), qs(h.modeName())));
        QString flags = h.singleInputStream() ? tr("SIS") : tr("MIS, ISI %1").arg(h.matype2);
        flags += h.ccm() ? tr(" · CCM") : tr(" · ACM/VCM");
        if (h.issyi())
            flags += tr(" · ISSY");
        if (h.npd())
            flags += tr(" · NPD");
        flags += tr(" · RO %1").arg(qs(h.rolloffName()));
        m_bbFlags->setText(QStringLiteral("0x%1%2 · %3")
                               .arg(h.matype1, 2, 16, QLatin1Char('0'))
                               .arg(h.matype2, 2, 16, QLatin1Char('0'))
                               .arg(flags));
        m_bbLengths->setText(QStringLiteral("%1 / %2 / %3 bits").arg(h.upl).arg(h.dfl).arg(h.syncd));
    }

    if (st.fileName.empty()) {
        m_file->setText(tr("off"));
    } else {
        QString details = QLocale().formattedDataSize(static_cast<qint64>(st.fileBytes));
        if (st.fileSegments > 1)
            details += tr(" · file %1").arg(st.fileSegments);
        if (st.fileFreeBytes >= 0)
            details += tr(" · %1 free").arg(QLocale().formattedDataSize(st.fileFreeBytes));
        if (st.fileStopped)
            details += QStringLiteral(" · <span style='color:%1'>● </span>%2").arg(Theme::tokens().critical.name(), tr("stopped"));
        m_file->setText(QStringLiteral("%1<br><span style='color:%2'>%3</span>")
                            .arg(qs(st.fileName).toHtmlEscaped(), Theme::tokens().muted.name(), details));
    }
    m_zmq->setText(tr("%1 messages · %2 dropped").arg(count(st.zmqMessages), errors(st.zmqDropped)));
}
