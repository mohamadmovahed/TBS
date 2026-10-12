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

    // ---- GSE -> pcap ----
    m_pcapOn = new ToggleSwitch;
    root->addLayout(sectionHeader(tr("Extract IP to pcap"), m_pcapOn));
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
    m_pcapFraming = new QComboBox;
    m_pcapFraming->addItem(tr("Ethernet (GSE label as MAC)"), true);
    m_pcapFraming->addItem(tr("Raw IP"), false);
    pc->addRow(keyLabel(tr("Framing")), m_pcapFraming);
    m_pcapNonIp = new QCheckBox(tr("Include non-IP PDUs (ARP, ...)"));
    pc->addRow(QString(), m_pcapNonIp);
    m_pcapTimestamp = new QCheckBox(tr("Append date and time"));
    pc->addRow(QString(), m_pcapTimestamp);
    root->addWidget(m_pcapBox);
    root->addWidget(hintLabel(tr("BBFrame capture of a GSE stream: IP packets are written for Wireshark/tcpdump. "
                                 "Transport streams are analysed (Services tab) regardless of this setting.")));
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
    return cfg;
}

void OutputPanel::setEditable(bool editable)
{
    m_editable = editable;
    m_pcapOn->setEnabled(editable);
    m_fileOn->setEnabled(editable);
    m_zmqOn->setEnabled(editable && ZmqSink::available());
    updateEnabled();
}

void OutputPanel::setCaptureMode(tbs::CaptureMode mode)
{
    m_mode = mode;
    const bool bb = mode == CaptureMode::BBFrame;
    m_bbFormat->setVisible(bb);
    m_bbFormatLabel->setVisible(bb);
    updateEnabled();
}

void OutputPanel::updateEnabled()
{
    m_fileBox->setEnabled(m_editable && m_fileOn->isChecked());
    m_zmqBox->setEnabled(m_editable && m_zmqOn->isChecked());
    m_packetsPerMsg->setEnabled(m_mode == CaptureMode::TransportStream);
    m_pcapBox->setEnabled(m_editable && m_pcapOn->isChecked());
    m_keepFiles->setEnabled(m_splitSize->value() > 0 || m_splitMinutes->value() > 0);
    m_pcapNonIp->setEnabled(m_pcapFraming->currentData().toBool());
}
