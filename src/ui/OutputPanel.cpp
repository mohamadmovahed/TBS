#include "OutputPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

OutputPanel::OutputPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    // ---- File ----
    m_fileGroup = new QGroupBox(tr("Write to file"));
    m_fileGroup->setCheckable(true);
    auto *file = new QFormLayout(m_fileGroup);
    auto *row = new QHBoxLayout;
    m_filePath = new QLineEdit;
    m_filePath->setPlaceholderText(tr("~/capture.ts"));
    auto *browse = new QPushButton(tr("Browse..."));
    row->addWidget(m_filePath, 1);
    row->addWidget(browse);
    file->addRow(tr("File:"), row);
    m_timestamp = new QCheckBox(tr("Append date/time to file name"));
    file->addRow(QString(), m_timestamp);
    m_bbFormat = new QComboBox;
    m_bbFormat->addItem(tr("Raw BBFrames (BBHeader + data field)"), static_cast<int>(BbFileFormat::Raw));
    m_bbFormat->addItem(tr("0xB8 + BBFrame (bbframe-tools format)"), static_cast<int>(BbFileFormat::B8Marker));
    m_bbFormat->addItem(tr("Pseudo-TS as received from the card"), static_cast<int>(BbFileFormat::PseudoTs));
    m_bbFormatLabel = new QLabel(tr("BBFrame format:"));
    file->addRow(m_bbFormatLabel, m_bbFormat);
    root->addWidget(m_fileGroup);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getSaveFileName(this, tr("Capture file"), m_filePath->text(),
                                                       tr("Transport stream (*.ts);;BBFrames (*.bbf);;All files (*)"));
        if (!f.isEmpty())
            m_filePath->setText(f);
    });

    // ---- ZeroMQ ----
    m_zmqGroup = new QGroupBox(tr("Send over ZeroMQ"));
    m_zmqGroup->setCheckable(true);
    auto *zmq = new QFormLayout(m_zmqGroup);
    m_endpoint = new QLineEdit;
    m_endpoint->setPlaceholderText(QStringLiteral("tcp://*:5555"));
    zmq->addRow(tr("Endpoint:"), m_endpoint);
    m_socketType = new QComboBox;
    m_socketType->addItem(QStringLiteral("PUB"), static_cast<int>(ZmqSocketKind::Pub));
    m_socketType->addItem(QStringLiteral("PUSH"), static_cast<int>(ZmqSocketKind::Push));
    zmq->addRow(tr("Socket:"), m_socketType);
    m_bindMode = new QComboBox;
    m_bindMode->addItem(tr("Bind (receivers connect)"), true);
    m_bindMode->addItem(tr("Connect (to a receiver)"), false);
    zmq->addRow(tr("Mode:"), m_bindMode);
    m_topic = new QLineEdit;
    m_topic->setPlaceholderText(tr("none"));
    m_topic->setToolTip(tr("If set, every message is sent as two frames: topic, then data"));
    zmq->addRow(tr("Topic frame:"), m_topic);
    m_header = new QCheckBox(tr("Prefix 20-byte metadata header (type, sequence, timestamp)"));
    zmq->addRow(QString(), m_header);
    m_packetsPerMsg = new QSpinBox;
    m_packetsPerMsg->setRange(1, 10000);
    m_packetsPerMsg->setToolTip(tr("TS mode: number of 188-byte packets per message (7 = 1316 bytes)"));
    zmq->addRow(tr("TS packets / msg:"), m_packetsPerMsg);
    m_hwm = new QSpinBox;
    m_hwm->setRange(1, 10000000);
    m_hwm->setToolTip(tr("Messages queued per peer before new ones are dropped"));
    zmq->addRow(tr("Send HWM:"), m_hwm);
#ifndef TBS_HAVE_ZMQ
    m_zmqGroup->setEnabled(false);
    m_zmqGroup->setTitle(tr("Send over ZeroMQ (not available in this build)"));
#endif
    root->addWidget(m_zmqGroup);

    // ---- Preview ----
    auto *prevGroup = new QGroupBox(tr("Preview"));
    auto *prev = new QVBoxLayout(prevGroup);
    m_video = new QCheckBox(tr("Decode and show video"));
#ifndef TBS_HAVE_FFMPEG
    m_video->setEnabled(false);
    m_video->setText(tr("Decode and show video (FFmpeg not available in this build)"));
#endif
    prev->addWidget(m_video);
    root->addWidget(prevGroup);
    root->addStretch();
}

void OutputPanel::setConfig(const TunerConfig &cfg)
{
    m_fileGroup->setChecked(cfg.fileEnabled);
    m_filePath->setText(cfg.filePath);
    m_timestamp->setChecked(cfg.fileTimestampSuffix);
    m_bbFormat->setCurrentIndex(m_bbFormat->findData(static_cast<int>(cfg.bbFileFormat)));
    m_zmqGroup->setChecked(cfg.zmqEnabled);
    m_endpoint->setText(cfg.zmqEndpoint);
    m_socketType->setCurrentIndex(m_socketType->findData(static_cast<int>(cfg.zmqSocket)));
    m_bindMode->setCurrentIndex(m_bindMode->findData(cfg.zmqBind));
    m_topic->setText(cfg.zmqTopic);
    m_header->setChecked(cfg.zmqMetadataHeader);
    m_packetsPerMsg->setValue(cfg.zmqTsPacketsPerMessage);
    m_hwm->setValue(cfg.zmqSendHwm);
    m_video->setChecked(cfg.videoPreview);
    setCaptureMode(cfg.captureMode);
}

void OutputPanel::applyTo(TunerConfig &cfg) const
{
    cfg.fileEnabled = m_fileGroup->isChecked();
    cfg.filePath = m_filePath->text().trimmed();
    cfg.fileTimestampSuffix = m_timestamp->isChecked();
    cfg.bbFileFormat = static_cast<BbFileFormat>(m_bbFormat->currentData().toInt());
#ifdef TBS_HAVE_ZMQ
    cfg.zmqEnabled = m_zmqGroup->isChecked();
#else
    cfg.zmqEnabled = false;
#endif
    cfg.zmqEndpoint = m_endpoint->text().trimmed().isEmpty() ? QStringLiteral("tcp://*:5555") : m_endpoint->text().trimmed();
    cfg.zmqSocket = static_cast<ZmqSocketKind>(m_socketType->currentData().toInt());
    cfg.zmqBind = m_bindMode->currentData().toBool();
    cfg.zmqTopic = m_topic->text();
    cfg.zmqMetadataHeader = m_header->isChecked();
    cfg.zmqTsPacketsPerMessage = m_packetsPerMsg->value();
    cfg.zmqSendHwm = m_hwm->value();
#ifdef TBS_HAVE_FFMPEG
    cfg.videoPreview = m_video->isChecked();
#else
    cfg.videoPreview = false;
#endif
}

void OutputPanel::setEditable(bool editable)
{
    m_fileGroup->setEnabled(editable);
#ifdef TBS_HAVE_ZMQ
    m_zmqGroup->setEnabled(editable);
#endif
}

void OutputPanel::setCaptureMode(CaptureMode mode)
{
    const bool bb = mode == CaptureMode::BBFrame;
    m_bbFormat->setVisible(bb);
    m_bbFormatLabel->setVisible(bb);
    m_packetsPerMsg->setEnabled(!bb);
}
