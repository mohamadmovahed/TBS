#include "IpPanel.h"

#include "../Theme.h"
#include "../widgets/MetricTile.h"
#include "../widgets/ShareBarDelegate.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace tbs::bb;

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString bytes(uint64_t b)
{
    return QLocale().formattedDataSize(static_cast<qint64>(b));
}

QString count(uint64_t v)
{
    return QLocale().toString(static_cast<qulonglong>(v));
}

QTableWidget *makeTable(const QStringList &headers, int shareColumn)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (shareColumn >= 0) {
        t->horizontalHeader()->setSectionResizeMode(shareColumn, QHeaderView::Stretch);
        t->setItemDelegateForColumn(shareColumn, new ShareBarDelegate(t));
    } else {
        t->horizontalHeader()->setStretchLastSection(true);
    }
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    t->setFrameShape(QFrame::NoFrame);
    return t;
}

QFrame *card(QWidget *content, const QString &title, int minHeight)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("Card"));
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(12, 4, 12, 10);
    v->addWidget(Theme::sectionTitle(title));
    v->addWidget(content, 1);
    f->setMinimumHeight(minHeight);
    return f;
}

void setCell(QTableWidget *t, int row, int col, const QString &text, bool right = false)
{
    QTableWidgetItem *item = t->item(row, col);
    if (!item) {
        item = new QTableWidgetItem;
        t->setItem(row, col, item);
    }
    item->setText(text);
    item->setToolTip(text);
    if (right)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void setShare(QTableWidget *t, int row, int col, double share)
{
    QTableWidgetItem *item = t->item(row, col);
    if (!item) {
        item = new QTableWidgetItem;
        t->setItem(row, col, item);
    }
    item->setData(ShareBarDelegate::kShareRole, share);
}

} // namespace

IpPanel::IpPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 12, 0, 0);
    m_stack = new QStackedWidget;
    outer->addWidget(m_stack);

    auto *empty = new QLabel(tr("No IP data found yet.\nIP statistics appear here for GSE (BBFrames of a generic stream) "
                                "or MPE (IP sections inside a transport stream)."));
    empty->setObjectName(QStringLiteral("Muted"));
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);

    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_stream = new QComboBox;
    m_stream->setVisible(false);
    m_stream->setMinimumWidth(160);
    m_gse = new QLabel;
    m_gse->setTextFormat(Qt::RichText);
    m_gse->setWordWrap(true);
    top->addWidget(m_stream);
    top->addWidget(m_gse, 1);
    v->addLayout(top);

    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(12);
    m_rate = new MetricTile(tr("IP rate"));
    m_pps = new MetricTile(tr("Packets / s"));
    m_pdus = new MetricTile(tr("IP PDUs"));
    m_errors = new MetricTile(tr("Errors"));
    m_rate->setValueFormat(2, QStringLiteral("Mbit/s"));
    m_pps->setValueFormat(0, QStringLiteral("pkt/s"));
    m_pdus->setValueFormat(0, tr("PDUs"));
    m_errors->setValueFormat(0, tr("new errors"));
    for (MetricTile *t : {m_rate, m_pps, m_pdus, m_errors}) {
        t->setSampleSeconds(1.0);
        tiles->addWidget(t);
    }
    v->addLayout(tiles);

    m_protocols = makeTable({tr("Protocol"), tr("Packets"), tr("Bytes"), tr("Share")}, 3);
    m_labels = makeTable({tr("GSE label / MAC"), tr("PDUs"), tr("Bytes"), tr("Share")}, 3);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(card(m_protocols, tr("Protocols"), 170), 1);
    row->addWidget(card(m_labels, tr("Labels"), 170), 1);
    v->addLayout(row);

    m_flows = makeTable({tr("Protocol"), tr("Source"), tr("Destination"), tr("Packets"), tr("Bytes"), tr("Mbit/s"), tr("Share")}, 6);
    v->addWidget(card(m_flows, tr("Top flows"), 220), 1);

    m_pcap = new QLabel;
    m_pcap->setObjectName(QStringLiteral("Hint"));
    m_pcap->setTextFormat(Qt::RichText);
    m_pcap->setWordWrap(true);
    m_pcap->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_pcap);
    m_stack->addWidget(page);
}

void IpPanel::reset()
{
    for (MetricTile *t : {m_rate, m_pps, m_pdus, m_errors}) {
        t->clearHistory();
        t->setValue(QStringLiteral("–"));
        t->setSubtitle(QString());
        t->setStatus(MetricTile::Status::None, QString());
    }
    m_protocols->setRowCount(0);
    m_labels->setRowCount(0);
    m_flows->setRowCount(0);
    m_gse->clear();
    m_pcap->clear();
    m_stream->clear();
    m_stream->setVisible(false);
    m_isis.clear();
    m_lastErrors = 0;
    m_stack->setCurrentIndex(0);
}

void IpPanel::updateSnapshot(const ProcessorSnapshot &snap)
{
    std::vector<int> isis;
    std::vector<const StreamReport *> reports;
    for (const auto &s : snap.streams) {
        if (s.content == StreamContent::Gse || s.content == StreamContent::GenericContinuous
            || s.content == StreamContent::RawIp || s.gse.pdus > 0 || s.mpe.datagrams > 0 || s.rawIp.packets > 0) {
            isis.push_back(s.isi);
            reports.push_back(&s);
        }
    }
    if (isis != m_isis) {
        m_isis = isis;
        const QSignalBlocker block(m_stream);
        const int previous = m_stream->currentIndex();
        m_stream->clear();
        for (size_t i = 0; i < isis.size(); ++i) {
            const bool mpe = reports[i]->mpe.datagrams > 0;
            const QString kind = mpe ? tr("MPE in TS") : tr("generic stream");
            m_stream->addItem(isis[i] < 0 ? kind : tr("ISI %1 · %2").arg(isis[i]).arg(kind));
        }
        m_stream->setCurrentIndex(std::clamp(previous, 0, std::max(0, m_stream->count() - 1)));
        m_stream->setVisible(isis.size() > 1);
    }

    const Theme::Tokens &tk = Theme::tokens();
    QString tun;
    if (snap.tunOpen)
        tun = tr(" &nbsp;·&nbsp; interface <b>%1</b> · %2 packets%3")
                  .arg(qs(snap.tunName).toHtmlEscaped())
                  .arg(count(snap.tunPackets))
                  .arg(snap.tunErrors ? tr(" · %1 not delivered").arg(count(snap.tunErrors)) : QString());
    else if (!snap.tunError.empty())
        tun = QStringLiteral(" &nbsp;·&nbsp; <span style='color:%1'>● %2</span>").arg(tk.critical.name(), qs(snap.tunError).toHtmlEscaped());
    if (!snap.pcapFile.empty())
        m_pcap->setText(tr("pcap <b>%1</b> · %2 packets · %3%4")
                            .arg(qs(snap.pcapFile).toHtmlEscaped())
                            .arg(count(snap.pcapPackets))
                            .arg(bytes(snap.pcapBytes))
                            .arg(snap.pcapError.empty() ? QString()
                                                        : QStringLiteral(" · <span style='color:%1'>● %2</span>")
                                                              .arg(tk.critical.name(), qs(snap.pcapError).toHtmlEscaped()))
                        + tun);
    else
        m_pcap->setText((snap.pcapError.empty() ? tr("pcap output is off (Outputs → Write IP to pcap)")
                                                : QStringLiteral("<span style='color:%1'>● %2</span>")
                                                      .arg(tk.critical.name(), qs(snap.pcapError).toHtmlEscaped()))
                        + tun);

    if (reports.empty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    const int idx = std::clamp(m_stream->currentIndex(), 0, static_cast<int>(reports.size()) - 1);
    const StreamReport &r = *reports[static_cast<size_t>(idx)];
    const GseStats &g = r.gse;
    const IpReport &ip = r.ip;

    // GSE / MPE health line
    const bool isGse = r.content == StreamContent::Gse;
    const bool isMpe = r.mpe.datagrams > 0;
    QStringList parts;
    if (isMpe) {
        QStringList pids;
        for (uint16_t p : r.mpe.pids)
            pids << QStringLiteral("0x%1").arg(p, 4, 16, QLatin1Char('0'));
        parts << QStringLiteral("<b>%1</b>").arg(tr("MPE on PID %1").arg(pids.join(QStringLiteral(", "))));
        parts << tr("sections %1").arg(count(r.mpe.sections));
        parts << tr("datagrams %1 (LLC/SNAP %2)").arg(count(r.mpe.datagrams), count(r.mpe.llcSnap));
        parts << tr("CRC errors %1").arg(count(r.mpe.crcErrors));
        if (r.mpe.scrambled)
            parts << tr("scrambled %1").arg(count(r.mpe.scrambled));
    }
    const bool isRawIp = r.content == StreamContent::RawIp;
    if (isRawIp) {
        parts << QStringLiteral("<b>%1</b>").arg(QString::fromLatin1(tbs::bb::genericDecoderName(r.decoder)));
        parts << tr("IPv4 packets %1").arg(count(r.rawIp.packets));
        parts << tr("skipped %1").arg(bytes(r.rawIp.skippedBytes));
    } else if (!isMpe || g.dataFields) {
        const bool nonStandard = isGse && r.decoder != tbs::bb::GenericDecoder::Gse;
        parts << QStringLiteral("<b>%1</b>")
                     .arg(nonStandard ? QString::fromLatin1(tbs::bb::genericDecoderName(r.decoder))
                                      : isGse ? tr("GSE detected") : tr("Generic stream (not GSE)"));
        parts << tr("data fields %1 (%2 valid)").arg(count(g.dataFields), count(g.validDataFields));
        parts << tr("GSE packets %1").arg(count(g.packets));
        parts << tr("reassembled %1").arg(count(g.reassembled));
        parts << tr("padding %1").arg(bytes(g.paddingBytes));
        parts << tr("labels 6B/3B/bcast/re-use %1/%2/%3/%4")
                     .arg(count(g.labelTypes[0]), count(g.labelTypes[1]), count(g.labelTypes[2]), count(g.labelTypes[3]));
    }
    if (r.frameCrc32)
        parts << tr("BBFrame CRC-32 %1").arg(count(r.frameCrc32));
    if (ip.nonIp)
        parts << tr("non-IP %1").arg(count(ip.nonIp));
    m_gse->setText(parts.join(QStringLiteral(" &nbsp;·&nbsp; ")));

    // Tiles
    m_rate->setValue(QString::number(ip.mbps, 'f', 2), QStringLiteral("Mbit/s"));
    m_rate->addSample(ip.mbps);
    m_rate->setSubtitle(tr("%1 total · %2 flows").arg(bytes(ip.ipBytes)).arg(ip.flowCount));
    m_pps->setValue(QString::number(ip.packetsPerSec, 'f', 0), tr("pkt/s"));
    m_pps->addSample(ip.packetsPerSec);
    m_pps->setSubtitle(tr("IPv4 %1 · IPv6 %2 · multicast %3").arg(count(ip.ipv4), count(ip.ipv6), count(ip.multicast)));
    m_pdus->setValue(count(ip.pdus));
    m_pdus->setSubtitle(isMpe ? tr("%1 MPE sections").arg(count(r.mpe.sections))
                              : tr("%1 complete · %2 reassembled").arg(count(g.completePdus), count(g.reassembled)));
    m_pdus->addSample(ip.packetsPerSec);
    const uint64_t errors = g.crcErrors + g.lengthErrors + g.fragmentErrors + ip.malformed + g.invalidDataFields
        + r.mpe.crcErrors + r.mpe.invalid;
    const uint64_t fresh = errors >= m_lastErrors ? errors - m_lastErrors : 0;
    m_lastErrors = errors;
    m_errors->setValue(count(errors), tr("total"));
    m_errors->addSample(static_cast<double>(fresh));
    m_errors->setSubtitle(tr("CRC %1 · length %2 · fragment %3 · malformed IP %4")
                              .arg(count(g.crcErrors + r.mpe.crcErrors), count(g.lengthErrors + r.mpe.invalid),
                                   count(g.fragmentErrors), count(ip.malformed)));
    m_errors->setStatus(fresh ? MetricTile::Status::Critical : MetricTile::Status::Good, fresh ? tr("Errors") : tr("Clean"));

    // Protocols (IP protocols, then non-IP EtherTypes)
    std::vector<ProtocolStat> protos = ip.protocols;
    for (const auto &e : ip.etherTypes)
        protos.push_back({qs(e.name).toStdString() + " (non-IP)", e.packets, e.bytes});
    uint64_t totalBytes = 0;
    for (const auto &p : protos)
        totalBytes += p.bytes;
    m_protocols->setRowCount(static_cast<int>(protos.size()));
    for (int i = 0; i < static_cast<int>(protos.size()); ++i) {
        const auto &p = protos[static_cast<size_t>(i)];
        setCell(m_protocols, i, 0, qs(p.name));
        setCell(m_protocols, i, 1, count(p.packets), true);
        setCell(m_protocols, i, 2, bytes(p.bytes), true);
        setShare(m_protocols, i, 3, totalBytes ? double(p.bytes) / double(totalBytes) : 0.0);
    }

    uint64_t labelBytes = 0;
    for (const auto &l : ip.labels)
        labelBytes += l.bytes;
    m_labels->setRowCount(static_cast<int>(ip.labels.size()));
    for (int i = 0; i < static_cast<int>(ip.labels.size()); ++i) {
        const auto &l = ip.labels[static_cast<size_t>(i)];
        setCell(m_labels, i, 0, qs(l.label));
        setCell(m_labels, i, 1, count(l.pdus), true);
        setCell(m_labels, i, 2, bytes(l.bytes), true);
        setShare(m_labels, i, 3, labelBytes ? double(l.bytes) / double(labelBytes) : 0.0);
    }

    m_flows->setRowCount(static_cast<int>(ip.topFlows.size()));
    for (int i = 0; i < static_cast<int>(ip.topFlows.size()); ++i) {
        const auto &f = ip.topFlows[static_cast<size_t>(i)];
        auto endpoint = [&](const std::string &addr, uint16_t port) {
            const QString a = f.ipVersion == 6 ? QStringLiteral("[%1]").arg(qs(addr)) : qs(addr);
            return port ? QStringLiteral("%1:%2").arg(a).arg(port) : qs(addr);
        };
        setCell(m_flows, i, 0, qs(f.protocolName));
        setCell(m_flows, i, 1, endpoint(f.src, f.srcPort));
        setCell(m_flows, i, 2, endpoint(f.dst, f.dstPort));
        setCell(m_flows, i, 3, count(f.packets), true);
        setCell(m_flows, i, 4, bytes(f.bytes), true);
        setCell(m_flows, i, 5, QString::number(f.mbps, 'f', 3), true);
        setShare(m_flows, i, 6, ip.ipBytes ? double(f.bytes) / double(ip.ipBytes) : 0.0);
    }
    m_protocols->viewport()->update();
    m_labels->viewport()->update();
    m_flows->viewport()->update();
}
