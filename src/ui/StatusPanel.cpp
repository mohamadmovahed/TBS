#include "StatusPanel.h"

#include "LedIndicator.h"
#include "core/DvbNames.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString formatBytes(uint64_t b)
{
    return QLocale().formattedDataSize(static_cast<qint64>(b));
}

QString formatCount(uint64_t v)
{
    return QLocale().toString(static_cast<qulonglong>(v));
}

QString errorStyle(uint64_t v)
{
    return v ? QStringLiteral("<span style='color:#d33'>%1</span>").arg(formatCount(v)) : formatCount(v);
}
} // namespace

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // ---- Frontend ----
    auto *feGroup = new QGroupBox(tr("Frontend"));
    auto *feLayout = new QVBoxLayout(feGroup);
    auto *leds = new QHBoxLayout;
    m_ledSignal = new LedIndicator(tr("Signal"));
    m_ledCarrier = new LedIndicator(tr("Carrier"));
    m_ledFec = new LedIndicator(tr("FEC"));
    m_ledSync = new LedIndicator(tr("Sync"));
    m_ledLock = new LedIndicator(tr("Lock"));
    for (LedIndicator *l : {m_ledSignal, m_ledCarrier, m_ledFec, m_ledSync, m_ledLock})
        leds->addWidget(l);
    leds->addStretch();
    feLayout->addLayout(leds);

    auto *bars = new QGridLayout;
    m_signalBar = new QProgressBar;
    m_signalBar->setRange(0, 1000);
    m_cnrBar = new QProgressBar;
    m_cnrBar->setRange(0, 1000);
    bars->addWidget(new QLabel(tr("Level")), 0, 0);
    bars->addWidget(m_signalBar, 0, 1);
    bars->addWidget(new QLabel(tr("C/N")), 1, 0);
    bars->addWidget(m_cnrBar, 1, 1);
    feLayout->addLayout(bars);

    auto *feGrid = new QGridLayout;
    m_delsys = addValue(feGrid, tr("System"));
    m_modcod = addValue(feGrid, tr("MODCOD"));
    m_rolloff = addValue(feGrid, tr("Roll-off / pilots"));
    m_freq = addValue(feGrid, tr("Frequency (IF)"));
    m_sr = addValue(feGrid, tr("Symbol rate"));
    m_streamId = addValue(feGrid, tr("Stream ID"));
    m_ber = addValue(feGrid, tr("BER (pre-FEC)"));
    m_ucb = addValue(feGrid, tr("Error blocks"));
    m_temp = addValue(feGrid, tr("Temperature"));
    feLayout->addLayout(feGrid);
    feLayout->addStretch();
    root->addWidget(feGroup, 3);

    // ---- Stream ----
    auto *right = new QVBoxLayout;
    auto *tsGroup = new QGroupBox(tr("Stream"));
    auto *tsGrid = new QGridLayout(tsGroup);
    m_rate = addValue(tsGrid, tr("Input rate"));
    m_bytes = addValue(tsGrid, tr("Received"));
    m_packets = addValue(tsGrid, tr("TS packets"));
    m_ccErrors = addValue(tsGrid, tr("CC errors"));
    m_tei = addValue(tsGrid, tr("TEI errors"));
    m_syncLoss = addValue(tsGrid, tr("Sync losses"));
    m_overflows = addValue(tsGrid, tr("Driver overflows"));
    right->addWidget(tsGroup);

    m_bbGroup = new QGroupBox(tr("BBFrames"));
    auto *bbGrid = new QGridLayout(m_bbGroup);
    m_bbFrames = addValue(bbGrid, tr("Frames"));
    m_bbRate = addValue(bbGrid, tr("Rate"));
    m_bbErrors = addValue(bbGrid, tr("Errors (CRC/seq/len)"));
    m_bbMatype = addValue(bbGrid, tr("MATYPE"));
    m_bbMatype->setWordWrap(true);
    m_bbLengths = addValue(bbGrid, tr("UPL / DFL / SYNCD"));
    right->addWidget(m_bbGroup);

    auto *outGroup = new QGroupBox(tr("Outputs"));
    auto *outGrid = new QGridLayout(outGroup);
    m_file = addValue(outGrid, tr("File"));
    m_file->setWordWrap(true);
    m_zmq = addValue(outGrid, tr("ZeroMQ"));
    right->addWidget(outGroup);
    right->addStretch();
    root->addLayout(right, 2);

    reset();
}

QLabel *StatusPanel::addValue(QGridLayout *grid, const QString &name)
{
    const int row = grid->rowCount();
    auto *key = new QLabel(name + QLatin1Char(':'));
    key->setAlignment(Qt::AlignRight | Qt::AlignTop);
    auto *val = new QLabel;
    val->setTextInteractionFlags(Qt::TextSelectableByMouse);
    val->setTextFormat(Qt::RichText);
    grid->addWidget(key, row, 0);
    grid->addWidget(val, row, 1);
    grid->setColumnStretch(1, 1);
    return val;
}

void StatusPanel::setBbFrameMode(bool on)
{
    m_bbGroup->setVisible(on);
}

void StatusPanel::reset()
{
    for (LedIndicator *l : {m_ledSignal, m_ledCarrier, m_ledFec, m_ledSync, m_ledLock})
        l->setOn(false);
    m_signalBar->setValue(0);
    m_signalBar->setFormat(QStringLiteral("-"));
    m_cnrBar->setValue(0);
    m_cnrBar->setFormat(QStringLiteral("-"));
    for (QLabel *l : {m_ber, m_ucb, m_temp, m_delsys, m_modcod, m_rolloff, m_freq, m_sr, m_streamId, m_rate,
                      m_bytes, m_packets, m_ccErrors, m_tei, m_syncLoss, m_overflows, m_bbFrames, m_bbRate,
                      m_bbErrors, m_bbMatype, m_bbLengths, m_file, m_zmq})
        l->setText(QStringLiteral("-"));
}

void StatusPanel::updateFrontend(const FrontendStatus &st)
{
    m_ledSignal->setOn(st.flags & FrontendStatus::HasSignal);
    m_ledCarrier->setOn(st.flags & FrontendStatus::HasCarrier);
    m_ledFec->setOn(st.flags & FrontendStatus::HasViterbi);
    m_ledSync->setOn(st.flags & FrontendStatus::HasSync);
    m_ledLock->setOn(st.flags & FrontendStatus::HasLock);

    if (st.hasSignalDbm) {
        // -90 dBm .. -10 dBm
        m_signalBar->setValue(static_cast<int>(std::clamp((st.signalDbm + 90.0) / 80.0, 0.0, 1.0) * 1000));
        m_signalBar->setFormat(QStringLiteral("%1 dBm").arg(st.signalDbm, 0, 'f', 1));
    } else if (st.hasSignalPercent) {
        m_signalBar->setValue(static_cast<int>(st.signalPercent * 10));
        m_signalBar->setFormat(QStringLiteral("%1 %").arg(st.signalPercent, 0, 'f', 0));
    } else {
        m_signalBar->setValue(0);
        m_signalBar->setFormat(QStringLiteral("-"));
    }

    if (st.hasCnrDb) {
        // 0 .. 25 dB
        m_cnrBar->setValue(static_cast<int>(std::clamp(st.cnrDb / 25.0, 0.0, 1.0) * 1000));
        m_cnrBar->setFormat(QStringLiteral("%1 dB").arg(st.cnrDb, 0, 'f', 1));
    } else if (st.hasCnrPercent) {
        m_cnrBar->setValue(static_cast<int>(st.cnrPercent * 10));
        m_cnrBar->setFormat(QStringLiteral("%1 %").arg(st.cnrPercent, 0, 'f', 0));
    } else {
        m_cnrBar->setValue(0);
        m_cnrBar->setFormat(QStringLiteral("-"));
    }

    m_ber->setText(st.hasBer ? QString::number(st.ber, 'e', 2) : QStringLiteral("-"));
    m_ucb->setText(st.hasErrorBlocks ? errorStyle(st.errorBlocks) : QStringLiteral("-"));
    m_temp->setText(st.hasTemperature ? QStringLiteral("%1 °C").arg(st.temperatureC, 0, 'f', 0) : QStringLiteral("-"));

    if (st.locked() && st.deliverySystem >= 0) {
        m_delsys->setText(DvbNames::deliverySystem(st.deliverySystem));
        m_modcod->setText(QStringLiteral("%1 %2").arg(DvbNames::modulation(st.modulation), DvbNames::fec(st.fec)));
        m_rolloff->setText(QStringLiteral("%1 / %2").arg(DvbNames::rolloff(st.rolloff), DvbNames::pilot(st.pilot)));
        m_freq->setText(QStringLiteral("%1 MHz").arg(st.frequencyKHz / 1000.0, 0, 'f', 3));
        m_sr->setText(QStringLiteral("%1 kS/s").arg(st.symbolRate / 1000.0, 0, 'f', 3));
        m_streamId->setText(st.streamId < 0 ? tr("none")
                                            : QStringLiteral("ISI %1 (0x%2)")
                                                  .arg(st.streamId & 0xFF)
                                                  .arg(static_cast<qulonglong>(st.streamId), 8, 16, QLatin1Char('0')));
    } else if (!st.locked()) {
        for (QLabel *l : {m_delsys, m_modcod, m_rolloff, m_freq, m_sr, m_streamId})
            l->setText(QStringLiteral("-"));
    }
}

void StatusPanel::updateStats(const StreamStats &st)
{
    m_rate->setText(QStringLiteral("%1 Mbit/s").arg(st.inputMbps, 0, 'f', 3));
    m_bytes->setText(formatBytes(st.bytesIn));
    m_packets->setText(formatCount(st.tsPackets));
    m_ccErrors->setText(errorStyle(st.ccErrors));
    m_tei->setText(errorStyle(st.teiErrors));
    m_syncLoss->setText(errorStyle(st.syncLosses));
    m_overflows->setText(errorStyle(st.driverOverflows));

    m_bbFrames->setText(formatCount(st.bbFrames));
    m_bbRate->setText(QStringLiteral("%1 frames/s, %2 Mbit/s payload")
                          .arg(st.bbFramesPerSec, 0, 'f', 1)
                          .arg(st.bbPayloadMbps, 0, 'f', 3));
    m_bbErrors->setText(QStringLiteral("%1 / %2 / %3")
                            .arg(errorStyle(st.bbCrcErrors), errorStyle(st.bbSequenceErrors), errorStyle(st.bbLengthErrors)));
    if (st.hasBbHeader) {
        const BbHeader &h = st.lastBbHeader;
        m_bbMatype->setText(QStringLiteral("0x%1%2: %3")
                                .arg(h.matype1, 2, 16, QLatin1Char('0'))
                                .arg(h.matype2, 2, 16, QLatin1Char('0'))
                                .arg(h.summary().toHtmlEscaped()));
        m_bbLengths->setText(QStringLiteral("%1 / %2 / %3 bits").arg(h.upl).arg(h.dfl).arg(h.syncd));
    }

    if (st.fileName.isEmpty())
        m_file->setText(tr("off"));
    else
        m_file->setText(QStringLiteral("%1<br>%2").arg(st.fileName.toHtmlEscaped(), formatBytes(st.fileBytes)));
    m_zmq->setText(QStringLiteral("%1 messages, %2 dropped").arg(formatCount(st.zmqMessages), errorStyle(st.zmqDropped)));
}
