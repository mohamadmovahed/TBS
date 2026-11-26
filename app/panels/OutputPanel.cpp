#include "OutputPanel.h"

#include "../Theme.h"
#include "../widgets/ToggleSwitch.h"
#include "FormHelpers.h"

#include <tbsdvb/Sink.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace tbs;

namespace {
QHBoxLayout *sectionHeader(const QString &title, ToggleSwitch *toggle)
{
    auto *row = new QHBoxLayout;
    row->addWidget(Theme::sectionTitle(title));
    row->addStretch();
    if (toggle)
        row->addWidget(toggle, 0, Qt::AlignBottom);
    return row;
}
} // namespace

OutputPanel::OutputPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 6, 18, 18);
    root->setSpacing(4);

    // ---- File ----
    m_fileOn = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Record to file"), m_fileOn));
    m_fileBox = new QWidget;
    auto *file = makeForm();
    m_fileBox->setLayout(file);
    auto *row = new QHBoxLayout;
    m_filePath = new QLineEdit;
    m_filePath->setPlaceholderText(tr("~/capture.ts"));
    auto *browse = new QPushButton;
    browse->setObjectName(QStringLiteral("Ghost"));
    browse->setIcon(Theme::icon(QStringLiteral("folder"), Theme::tokens().ink2));
    row->addWidget(m_filePath, 1);
    row->addWidget(browse);
    file->addRow(keyLabel(tr("File")), row);
    m_timestamp = new QCheckBox(tr("Append date and time"));
    file->addRow(QString(), m_timestamp);
    m_bbFormat = new QComboBox;
    m_bbFormat->addItem(tr("Raw BBFrames"), static_cast<int>(BbFileFormat::Raw));
    m_bbFormat->addItem(tr("0xB8 + BBFrame (bbframe-tools)"), static_cast<int>(BbFileFormat::B8Marker));
    m_bbFormat->addItem(tr("Pseudo-TS as received"), static_cast<int>(BbFileFormat::PseudoTs));
    m_bbFormatLabel = keyLabel(tr("Format"));
    file->addRow(m_bbFormatLabel, m_bbFormat);
    auto spin = [](int max, const QString &suffix, const QString &zero) {
        auto *sp = new QSpinBox;
        sp->setRange(0, max);
        sp->setSuffix(suffix);
        sp->setSpecialValueText(zero);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return sp;
    };
    auto *split = new QHBoxLayout;
    m_splitSize = spin(1000000, tr(" MiB"), tr("no size limit"));
    m_splitMinutes = spin(100000, tr(" min"), tr("no time limit"));
    split->addWidget(m_splitSize, 1);
    split->addWidget(keyLabel(tr("or")));
    split->addWidget(m_splitMinutes, 1);
    file->addRow(keyLabel(tr("New file every")), split);
    m_keepFiles = spin(100000, tr(" files"), tr("keep all"));
    m_keepFiles->setToolTip(tr("Rolling recording: delete the oldest files of this recording beyond this number"));
    file->addRow(keyLabel(tr("Keep newest")), m_keepFiles);
    m_minFree = spin(10000000, tr(" MiB"), tr("no check"));
    m_minFree->setToolTip(tr("Stop recording cleanly when the free space on the disk drops below this"));
    file->addRow(keyLabel(tr("Stop below")), m_minFree);
    root->addWidget(m_fileBox);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getSaveFileName(this, tr("Capture file"), m_filePath->text(),
                                                       tr("Transport stream (*.ts);;BBFrames (*.bbf);;All files (*)"));
        if (!f.isEmpty())
            m_filePath->setText(f);
    });

    // ---- ZeroMQ ----
    m_zmqOn = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Publish over ZeroMQ"), m_zmqOn));
    m_zmqBox = new QWidget;
    auto *zmq = makeForm();
    m_zmqBox->setLayout(zmq);
    m_endpoint = new QLineEdit;
    m_endpoint->setPlaceholderText(QStringLiteral("tcp://*:5555"));
    zmq->addRow(keyLabel(tr("Endpoint")), m_endpoint);
    m_socketType = new QComboBox;
    m_socketType->addItem(tr("PUB — fan out, drops when slow"), static_cast<int>(ZmqSocketKind::Pub));
    m_socketType->addItem(tr("PUSH — queued, lossless until HWM"), static_cast<int>(ZmqSocketKind::Push));
    zmq->addRow(keyLabel(tr("Socket")), m_socketType);
    m_bindMode = new QComboBox;
    m_bindMode->addItem(tr("Bind (clients connect)"), true);
    m_bindMode->addItem(tr("Connect (to a client)"), false);
    zmq->addRow(keyLabel(tr("Mode")), m_bindMode);
    m_topic = new QLineEdit;
    m_topic->setPlaceholderText(tr("none"));
    m_topic->setToolTip(tr("If set, each message is multipart: topic, then data"));
    zmq->addRow(keyLabel(tr("Topic")), m_topic);
    m_packetsPerMsg = new QSpinBox;
    m_packetsPerMsg->setRange(1, 10000);
    m_packetsPerMsg->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_packetsPerMsg->setToolTip(tr("TS mode: 188-byte packets per message (7 = 1316 bytes)"));
    zmq->addRow(keyLabel(tr("TS packets/msg")), m_packetsPerMsg);
    m_hwm = new QSpinBox;
    m_hwm->setRange(1, 10000000);
    m_hwm->setButtonSymbols(QAbstractSpinBox::NoButtons);
    zmq->addRow(keyLabel(tr("Send HWM")), m_hwm);
    m_header = new QCheckBox(tr("20-byte metadata header"));
    m_header->setToolTip(tr("\"TBS1\", type, sequence number and timestamp before each payload"));
    zmq->addRow(QString(), m_header);
    root->addWidget(m_zmqBox);
    if (!ZmqSink::available()) {
        m_zmqOn->setEnabled(false);
        root->addWidget(hintLabel(tr("This build has no ZeroMQ support.")));
    }

    // ---- TS over the network ----
    root->addLayout(sectionHeader(tr("Stream TS to network"), nullptr));
    auto *netBox = new QWidget;
    m_netList = new QVBoxLayout(netBox);
    m_netList->setContentsMargins(0, 4, 0, 4);
    m_netList->setSpacing(6);
    m_netEmpty = hintLabel(tr("No network outputs. Send the transport stream, or one service of it, as UDP/RTP "
                              "multicast or over SRT."));
    m_netList->addWidget(m_netEmpty);
    root->addWidget(netBox);
    m_netAdd = new QPushButton(tr("Add network output…"));
    m_netAdd->setObjectName(QStringLiteral("Ghost"));
    root->addWidget(m_netAdd, 0, Qt::AlignLeft);
    connect(m_netAdd, &QPushButton::clicked, this, [this]() {
        TsNetworkOutput o;
        NetOutputDialog dlg(o, m_services, m_mode == CaptureMode::BBFrame, this);
        if (dlg.exec() == QDialog::Accepted)
            addNetRow(dlg.output());
    });

    // ---- Generic stream decoding (standard GSE or a non-standard variant) ----
    root->addLayout(sectionHeader(tr("Generic stream decoding"), nullptr));
    auto *gd = makeForm();
    m_genericDecoder = new QComboBox;
    for (int i = 0; i < tbs::bb::kGenericDecoderCount; ++i) {
        const auto dec = static_cast<tbs::bb::GenericDecoder>(i);
        m_genericDecoder->addItem(QString::fromLatin1(tbs::bb::genericDecoderName(dec)), i);
    }
    gd->addRow(keyLabel(tr("Decoder")), m_genericDecoder);
    root->addLayout(gd);
    root->addWidget(hintLabel(tr("Auto detect tries standard GSE and the non-standard encapsulations found on real "
                                 "carriers (GSE dialects, TS or raw IPv4 inside a generic stream) on each input stream, "
                                 "and keeps the one that fits. Takes effect at the next start.")));

    // ---- GSE -> pcap ----
    m_pcapOn = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Write IP to pcap"), m_pcapOn));
    m_pcapBox = new QWidget;
    auto *pc = makeForm();
    m_pcapBox->setLayout(pc);
    auto *prow = new QHBoxLayout;
    m_pcapPath = new QLineEdit;
    m_pcapPath->setPlaceholderText(tr("~/gse.pcap"));
    auto *pbrowse = new QPushButton;
    pbrowse->setObjectName(QStringLiteral("Ghost"));
    pbrowse->setIcon(Theme::icon(QStringLiteral("folder"), Theme::tokens().ink2));
    prow->addWidget(m_pcapPath, 1);
    prow->addWidget(pbrowse);
    pc->addRow(keyLabel(tr("File")), prow);
    m_pcapFormat = new QComboBox;
    m_pcapFormat->addItem(tr("pcap"), static_cast<int>(tbs::bb::PcapFormat::Pcap));
    m_pcapFormat->addItem(tr("pcapng (one interface per stream)"), static_cast<int>(tbs::bb::PcapFormat::PcapNg));
    pc->addRow(keyLabel(tr("Format")), m_pcapFormat);
    m_pcapFraming = new QComboBox;
    m_pcapFraming->addItem(tr("Ethernet (GSE label as MAC)"), true);
    m_pcapFraming->addItem(tr("Raw IP"), false);
    pc->addRow(keyLabel(tr("Framing")), m_pcapFraming);
    m_pcapNonIp = new QCheckBox(tr("Include non-IP PDUs (ARP, ...)"));
    pc->addRow(QString(), m_pcapNonIp);
    m_pcapTimestamp = new QCheckBox(tr("Append date and time"));
    pc->addRow(QString(), m_pcapTimestamp);
    root->addWidget(m_pcapBox);
    root->addWidget(hintLabel(tr("IP packets from GSE (BBFrame capture of a generic stream) or MPE (inside a transport "
                                 "stream) are written for Wireshark/tcpdump.")));

    // ---- TUN/TAP ----
    m_tunOn = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Send IP to network interface"), m_tunOn));
    m_tunBox = new QWidget;
    auto *tf = makeForm();
    m_tunBox->setLayout(tf);
    m_tunName = new QLineEdit;
    m_tunName->setPlaceholderText(QStringLiteral("tbs0"));
    tf->addRow(keyLabel(tr("Interface")), m_tunName);
    m_tunMode = new QComboBox;
    m_tunMode->addItem(tr("TUN (IP packets)"), false);
    m_tunMode->addItem(tr("TAP (Ethernet, label as MAC)"), true);
    tf->addRow(keyLabel(tr("Mode")), m_tunMode);
    root->addWidget(m_tunBox);
    if (tbs::bb::TunWriter::supported()) {
        root->addWidget(hintLabel(tr("Received IP packets appear on this interface for routing, multicast receivers or "
                                     "tcpdump. Without root, create it once: <tt>sudo ip tuntap add dev tbs0 mode tun user "
                                     "$USER</tt>")));
    } else {
        m_tunOn->setEnabled(false);
        root->addWidget(hintLabel(tr("TUN/TAP output is only available on Linux.")));
    }
    connect(m_tunOn, &ToggleSwitch::toggled, this, &OutputPanel::updateEnabled);
    connect(pbrowse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getSaveFileName(this, tr("pcap file"), m_pcapPath->text(), tr("pcap (*.pcap);;All files (*)"));
        if (!f.isEmpty())
            m_pcapPath->setText(f);
    });
    connect(m_pcapOn, &ToggleSwitch::toggled, this, &OutputPanel::updateEnabled);
    connect(m_pcapFraming, qOverload<int>(&QComboBox::currentIndexChanged), this, &OutputPanel::updateEnabled);

    // ---- Preview ----
    m_video = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Video preview"), m_video));
#ifdef TBS_HAVE_FFMPEG
    root->addWidget(hintLabel(tr("Decodes the first video stream with FFmpeg (video only). Can be switched while running.")));
#else
    m_video->setEnabled(false);
    root->addWidget(hintLabel(tr("This build has no FFmpeg support.")));
#endif
    root->addStretch();

    connect(m_fileOn, &ToggleSwitch::toggled, this, &OutputPanel::updateEnabled);
    connect(m_splitSize, qOverload<int>(&QSpinBox::valueChanged), this, &OutputPanel::updateEnabled);
    connect(m_splitMinutes, qOverload<int>(&QSpinBox::valueChanged), this, &OutputPanel::updateEnabled);
    connect(m_zmqOn, &ToggleSwitch::toggled, this, &OutputPanel::updateEnabled);
    connect(m_video, &ToggleSwitch::toggled, this, &OutputPanel::videoPreviewToggled);
    // Long item texts must not widen the sidebar
    for (QComboBox *c : findChildren<QComboBox *>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(8);
    }
    updateEnabled();
}

void OutputPanel::setConfig(const tbs::ReceiverConfig &cfg, bool videoPreview)
{
    m_fileOn->setChecked(cfg.file.enabled);
    m_filePath->setText(QString::fromStdString(cfg.file.path));
    m_timestamp->setChecked(cfg.file.timestampSuffix);
    m_bbFormat->setCurrentIndex(m_bbFormat->findData(static_cast<int>(cfg.file.bbFormat)));
    m_splitSize->setValue(static_cast<int>(cfg.file.splitSizeMB));
    m_splitMinutes->setValue(cfg.file.splitMinutes);
    m_keepFiles->setValue(cfg.file.keepFiles);
    m_minFree->setValue(static_cast<int>(cfg.file.minFreeMB));
    m_zmqOn->setChecked(cfg.zmq.enabled && ZmqSink::available());
    m_endpoint->setText(QString::fromStdString(cfg.zmq.endpoint));
    m_socketType->setCurrentIndex(m_socketType->findData(static_cast<int>(cfg.zmq.socket)));
    m_bindMode->setCurrentIndex(m_bindMode->findData(cfg.zmq.bind));
    m_topic->setText(QString::fromStdString(cfg.zmq.topic));
    m_header->setChecked(cfg.zmq.metadataHeader);
    m_packetsPerMsg->setValue(cfg.zmq.tsPacketsPerMessage);
    m_hwm->setValue(cfg.zmq.sendHwm);
    clearNetRows();
    for (const TsNetworkOutput &o : cfg.network)
        addNetRow(o);
#ifdef TBS_HAVE_FFMPEG
    m_video->setChecked(videoPreview);
#else
    Q_UNUSED(videoPreview);
#endif
    setCaptureMode(cfg.capture);
}

void OutputPanel::applyTo(tbs::ReceiverConfig &cfg) const
{
    cfg.file.enabled = m_fileOn->isChecked();
    cfg.file.path = m_filePath->text().trimmed().toStdString();
    cfg.file.timestampSuffix = m_timestamp->isChecked();
    cfg.file.bbFormat = static_cast<BbFileFormat>(m_bbFormat->currentData().toInt());
    cfg.file.splitSizeMB = static_cast<uint64_t>(m_splitSize->value());
    cfg.file.splitMinutes = m_splitMinutes->value();
    cfg.file.keepFiles = m_keepFiles->value();
    cfg.file.minFreeMB = static_cast<uint64_t>(m_minFree->value());
    cfg.zmq.enabled = m_zmqOn->isChecked() && ZmqSink::available();
    const QString ep = m_endpoint->text().trimmed();
    cfg.zmq.endpoint = ep.isEmpty() ? std::string("tcp://*:5555") : ep.toStdString();
    cfg.zmq.socket = static_cast<ZmqSocketKind>(m_socketType->currentData().toInt());
    cfg.zmq.bind = m_bindMode->currentData().toBool();
    cfg.zmq.topic = m_topic->text().toStdString();
    cfg.zmq.metadataHeader = m_header->isChecked();
    cfg.zmq.tsPacketsPerMessage = m_packetsPerMsg->value();
    cfg.zmq.sendHwm = m_hwm->value();
    cfg.network.clear();
    for (const NetRow &r : m_netRows) {
        TsNetworkOutput o = r.output;
        o.enabled = r.on->isChecked();
        cfg.network.push_back(o);
    }
}

bool OutputPanel::videoPreview() const
{
#ifdef TBS_HAVE_FFMPEG
    return m_video->isChecked();
#else
    return false;
#endif
}

void OutputPanel::setProcessing(const tbs::bb::ProcessorConfig &cfg)
{
    m_pcapOn->setChecked(cfg.pcap.enabled);
    m_pcapPath->setText(QString::fromStdString(cfg.pcap.path));
    m_pcapFraming->setCurrentIndex(m_pcapFraming->findData(cfg.pcap.ethernetFraming));
    m_pcapNonIp->setChecked(!cfg.pcap.ipOnly);
    m_pcapTimestamp->setChecked(cfg.pcap.timestampSuffix);
    m_pcapFormat->setCurrentIndex(m_pcapFormat->findData(static_cast<int>(cfg.pcap.format)));
    m_genericDecoder->setCurrentIndex(m_genericDecoder->findData(static_cast<int>(cfg.genericDecoder)));
    m_tunOn->setChecked(cfg.tun.enabled && tbs::bb::TunWriter::supported());
    m_tunName->setText(QString::fromStdString(cfg.tun.name));
    m_tunMode->setCurrentIndex(m_tunMode->findData(cfg.tun.tap));
    updateEnabled();
}

tbs::bb::ProcessorConfig OutputPanel::processing() const
{
    tbs::bb::ProcessorConfig cfg;
    cfg.pcap.enabled = m_pcapOn->isChecked();
    cfg.pcap.path = m_pcapPath->text().trimmed().toStdString();
    cfg.pcap.ethernetFraming = m_pcapFraming->currentData().toBool();
    cfg.pcap.ipOnly = !m_pcapNonIp->isChecked();
    cfg.pcap.timestampSuffix = m_pcapTimestamp->isChecked();
    cfg.pcap.format = static_cast<tbs::bb::PcapFormat>(m_pcapFormat->currentData().toInt());
    cfg.genericDecoder = static_cast<tbs::bb::GenericDecoder>(m_genericDecoder->currentData().toInt());
    cfg.tun.enabled = m_tunOn->isChecked() && tbs::bb::TunWriter::supported();
    const QString tunName = m_tunName->text().trimmed();
    cfg.tun.name = tunName.isEmpty() ? std::string("tbs0") : tunName.toStdString();
    cfg.tun.tap = m_tunMode->currentData().toBool();
    return cfg;
}

void OutputPanel::setEditable(bool editable)
{
    m_editable = editable;
    m_pcapOn->setEnabled(editable);
    m_tunOn->setEnabled(editable && tbs::bb::TunWriter::supported());
    m_fileOn->setEnabled(editable);
    m_zmqOn->setEnabled(editable && ZmqSink::available());
    m_netAdd->setEnabled(editable);
    for (NetRow &r : m_netRows) {
        r.on->setEnabled(editable);
        r.edit->setEnabled(editable);
        r.remove->setEnabled(editable);
        if (editable)
            r.status->clear();
        r.status->setVisible(!editable && r.on->isChecked());
    }
    updateEnabled();
}

void OutputPanel::setCaptureMode(tbs::CaptureMode mode)
{
    m_mode = mode;
    const bool bb = mode == CaptureMode::BBFrame;
    m_bbFormat->setVisible(bb);
    m_bbFormatLabel->setVisible(bb);
    for (NetRow &r : m_netRows)
        refreshNetRow(r);
    updateEnabled();
}

void OutputPanel::setServices(const QVector<ServiceChoice> &services)
{
    m_services = services;
}

void OutputPanel::addNetRow(const TsNetworkOutput &o)
{
    NetRow r;
    r.output = o;
    r.widget = new QWidget;
    r.widget->setObjectName(QStringLiteral("Card"));
    r.widget->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(r.widget);
    h->setContentsMargins(12, 8, 8, 8);
    h->setSpacing(10);
    r.on = new ToggleSwitch;
    r.on->setChecked(o.enabled);
    r.on->setToolTip(tr("Send to this output on the next start"));
    h->addWidget(r.on, 0, Qt::AlignVCenter);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    r.title = new QLabel;
    r.title->setObjectName(QStringLiteral("ValueLabel"));
    r.detail = new QLabel;
    r.detail->setObjectName(QStringLiteral("Muted"));
    r.status = new QLabel;
    r.status->setObjectName(QStringLiteral("Muted"));
    r.status->setTextFormat(Qt::RichText);
    r.status->setVisible(false);
    for (QLabel *l : {r.title, r.detail, r.status}) {
        l->setWordWrap(true);
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->addWidget(l);
    }
    h->addLayout(text, 1);
    r.edit = new QPushButton(tr("Edit"));
    r.edit->setObjectName(QStringLiteral("Ghost"));
    r.remove = new QPushButton(tr("Remove"));
    r.remove->setObjectName(QStringLiteral("Ghost"));
    h->addWidget(r.edit, 0, Qt::AlignVCenter);
    h->addWidget(r.remove, 0, Qt::AlignVCenter);
    QWidget *w = r.widget;
    connect(r.edit, &QPushButton::clicked, this, [this, w]() { editNetRow(w); });
    connect(r.remove, &QPushButton::clicked, this, [this, w]() { removeNetRow(w); });
    m_netList->addWidget(w);
    m_netRows.push_back(r);
    refreshNetRow(m_netRows.back());
    m_netEmpty->setVisible(false);
}

void OutputPanel::refreshNetRow(NetRow &r)
{
    const TsNetworkOutput &o = r.output;
    const QString proto = o.protocol == NetProtocol::Rtp ? QStringLiteral("RTP")
                          : o.protocol == NetProtocol::Srt ? QStringLiteral("SRT")
                                                           : QStringLiteral("UDP");
    const bool listener = o.protocol == NetProtocol::Srt && o.srtMode == SrtMode::Listener;
    const QString host = o.host.empty() ? QStringLiteral("*") : QString::fromStdString(o.host);
    const QString hostPort = host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]:%2").arg(host).arg(o.port)
                                                             : QStringLiteral("%1:%2").arg(host).arg(o.port);
    r.title->setText(listener ? tr("%1 listener on %2").arg(proto, hostPort) : QStringLiteral("%1 %2").arg(proto, hostPort));
    QStringList d;
    if (o.programNumber > 0) {
        QString name;
        for (const ServiceChoice &s : m_services)
            if (s.programNumber == o.programNumber && (o.isi < 0 || s.isi < 0 || s.isi == o.isi))
                name = s.name;
        d << (name.isEmpty() ? tr("service %1").arg(o.programNumber) : tr("service %1 (%2)").arg(o.programNumber).arg(name));
    } else {
        d << (o.pids.empty() ? tr("whole TS") : tr("%n PID(s)", nullptr, static_cast<int>(o.pids.size())));
    }
    if (o.programNumber > 0 && !o.pids.empty())
        d << tr("+%n PID(s)", nullptr, static_cast<int>(o.pids.size()));
    if (m_mode == CaptureMode::BBFrame)
        d << (o.isi >= 0 ? tr("ISI %1").arg(o.isi) : tr("first TS input stream"));
    if (o.protocol == NetProtocol::Srt) {
        d << tr("latency %1 ms").arg(o.srtLatencyMs);
        if (!o.srtPassphrase.empty())
            d << tr("encrypted");
    } else if (!o.interfaceName.empty()) {
        d << tr("via %1").arg(QString::fromStdString(o.interfaceName));
    }
    r.detail->setText(d.join(QStringLiteral(" · ")));
    r.widget->setToolTip(QString::fromStdString(networkUrl(o, true)));
}

void OutputPanel::editNetRow(QWidget *w)
{
    for (NetRow &r : m_netRows) {
        if (r.widget != w)
            continue;
        NetOutputDialog dlg(r.output, m_services, m_mode == CaptureMode::BBFrame, this);
        if (dlg.exec() == QDialog::Accepted) {
            r.output = dlg.output();
            refreshNetRow(r);
        }
        return;
    }
}

void OutputPanel::removeNetRow(QWidget *w)
{
    for (int i = 0; i < m_netRows.size(); ++i) {
        if (m_netRows[i].widget == w) {
            m_netRows.removeAt(i);
            w->deleteLater();
            break;
        }
    }
    m_netEmpty->setVisible(m_netRows.isEmpty());
}

void OutputPanel::clearNetRows()
{
    for (NetRow &r : m_netRows)
        delete r.widget;
    m_netRows.clear();
    m_netEmpty->setVisible(true);
}

void OutputPanel::setNetworkStatus(const std::vector<tbs::NetOutputStatus> &status)
{
    size_t i = 0;
    const Theme::Tokens &t = Theme::tokens();
    for (NetRow &r : m_netRows) {
        if (!r.on->isChecked())
            continue;
        if (i >= status.size())
            break;
        const NetOutputStatus &s = status[i++];
        const QColor dot = s.active ? t.good : s.sendErrors ? t.warning : t.muted;
        QString text = QStringLiteral("<span style=\"color:%1\">●</span> ").arg(dot.name());
        if (s.active)
            text += tr("%1 Mbit/s · ").arg(s.mbps, 0, 'f', 2);
        text += QString::fromStdString(s.state).toHtmlEscaped();
        if (s.sendErrors)
            text += tr(" · %1 dropped").arg(s.sendErrors);
        r.status->setText(text);
        r.status->setVisible(true);
    }
}

void OutputPanel::updateEnabled()
{
    m_fileBox->setEnabled(m_editable && m_fileOn->isChecked());
    m_zmqBox->setEnabled(m_editable && m_zmqOn->isChecked());
    m_packetsPerMsg->setEnabled(m_mode == CaptureMode::TransportStream);
    m_pcapBox->setEnabled(m_editable && m_pcapOn->isChecked());
    m_tunBox->setEnabled(m_editable && m_tunOn->isChecked());
    m_keepFiles->setEnabled(m_splitSize->value() > 0 || m_splitMinutes->value() > 0);
    m_pcapNonIp->setEnabled(m_pcapFraming->currentData().toBool());
}
