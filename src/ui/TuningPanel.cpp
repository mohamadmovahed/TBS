#include "TuningPanel.h"

#include "core/PlsUtil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *makeMHz(double min, double max, int decimals = 3)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSuffix(QStringLiteral(" MHz"));
    s->setAccelerated(true);
    return s;
}
} // namespace

TuningPanel::TuningPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    // ---- Device ----
    auto *devGroup = new QGroupBox(tr("Device"));
    auto *devLayout = new QFormLayout(devGroup);
    auto *devRow = new QHBoxLayout;
    m_device = new QComboBox;
    m_device->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_device->setMinimumContentsLength(24);
    m_refresh = new QPushButton(tr("Rescan"));
    devRow->addWidget(m_device, 1);
    devRow->addWidget(m_refresh);
    devLayout->addRow(tr("Receiver:"), devRow);

    m_dvbBox = new QWidget;
    auto *dvbRow = new QHBoxLayout(m_dvbBox);
    dvbRow->setContentsMargins(0, 0, 0, 0);
    m_adapter = new QSpinBox;
    m_adapter->setRange(0, 63);
    m_frontend = new QSpinBox;
    m_frontend->setRange(0, 15);
    m_demux = new QSpinBox;
    m_demux->setRange(0, 15);
    dvbRow->addWidget(new QLabel(tr("Adapter")));
    dvbRow->addWidget(m_adapter);
    dvbRow->addWidget(new QLabel(tr("Frontend")));
    dvbRow->addWidget(m_frontend);
    dvbRow->addWidget(new QLabel(tr("Demux")));
    dvbRow->addWidget(m_demux);
    dvbRow->addStretch();
    devLayout->addRow(m_dvbBox);

    m_replayBox = new QWidget;
    auto *replayLayout = new QFormLayout(m_replayBox);
    replayLayout->setContentsMargins(0, 0, 0, 0);
    auto *fileRow = new QHBoxLayout;
    m_replayFile = new QLineEdit;
    auto *browse = new QPushButton(tr("Browse..."));
    fileRow->addWidget(m_replayFile, 1);
    fileRow->addWidget(browse);
    replayLayout->addRow(tr("File:"), fileRow);
    m_replayRate = new QDoubleSpinBox;
    m_replayRate->setRange(0, 1000);
    m_replayRate->setDecimals(3);
    m_replayRate->setSuffix(QStringLiteral(" Mbit/s"));
    m_replayRate->setSpecialValueText(tr("Unthrottled"));
    replayLayout->addRow(tr("Rate:"), m_replayRate);
    m_replayLoop = new QCheckBox(tr("Loop"));
    replayLayout->addRow(QString(), m_replayLoop);
    devLayout->addRow(m_replayBox);
    root->addWidget(devGroup);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Select recording"), m_replayFile->text(),
                                                       tr("Transport streams (*.ts *.trp *.mpg *.bin);;All files (*)"));
        if (!f.isEmpty())
            m_replayFile->setText(f);
    });

    // ---- Carrier ----
    auto *carrierGroup = new QGroupBox(tr("Carrier"));
    auto *carrier = new QFormLayout(carrierGroup);
    m_frequency = makeMHz(0, 30000);
    carrier->addRow(tr("Frequency (RF):"), m_frequency);
    m_symbolRate = new QDoubleSpinBox;
    m_symbolRate->setRange(100, 520000);
    m_symbolRate->setDecimals(3);
    m_symbolRate->setSuffix(QStringLiteral(" kS/s"));
    m_symbolRate->setAccelerated(true);
    carrier->addRow(tr("Symbol rate:"), m_symbolRate);
    m_delsys = new QComboBox;
    m_delsys->addItem(tr("DVB-S"), static_cast<int>(DeliverySystem::DvbS));
    m_delsys->addItem(tr("DVB-S2 / S2X"), static_cast<int>(DeliverySystem::DvbS2));
    carrier->addRow(tr("Standard:"), m_delsys);
    m_modulation = new QComboBox;
    m_modulation->addItem(tr("Auto"), static_cast<int>(ModulationHint::Auto));
    m_modulation->addItem(QStringLiteral("QPSK"), static_cast<int>(ModulationHint::Qpsk));
    m_modulation->addItem(QStringLiteral("8PSK"), static_cast<int>(ModulationHint::Psk8));
    m_modulation->addItem(QStringLiteral("16APSK"), static_cast<int>(ModulationHint::Apsk16));
    m_modulation->addItem(QStringLiteral("32APSK"), static_cast<int>(ModulationHint::Apsk32));
    m_modulation->setToolTip(tr("The STiD135 detects modulation and code rate automatically; "
                                "a hint is only needed for other demodulators."));
    carrier->addRow(tr("Modulation:"), m_modulation);
    root->addWidget(carrierGroup);

    // ---- LNB ----
    auto *lnbGroup = new QGroupBox(tr("LNB / switch"));
    auto *lnb = new QFormLayout(lnbGroup);
    m_lnbType = new QComboBox;
    m_lnbType->addItem(tr("None (L-band input)"), static_cast<int>(LnbType::Direct));
    m_lnbType->addItem(tr("Universal Ku (2 LO)"), static_cast<int>(LnbType::Universal));
    m_lnbType->addItem(tr("Single LO (C band, Ku...)"), static_cast<int>(LnbType::SingleLo));
    lnb->addRow(tr("LNB type:"), m_lnbType);
    m_lo1 = makeMHz(0, 30000);
    lnb->addRow(tr("LO (low):"), m_lo1);
    m_lo2 = makeMHz(0, 30000);
    lnb->addRow(tr("LO (high):"), m_lo2);
    m_switch = makeMHz(0, 30000);
    lnb->addRow(tr("Band switch:"), m_switch);
    m_polarization = new QComboBox;
    m_polarization->addItem(tr("Vertical / Right (13 V)"), static_cast<int>(Polarization::VerticalRight13V));
    m_polarization->addItem(tr("Horizontal / Left (18 V)"), static_cast<int>(Polarization::HorizontalLeft18V));
    m_polarization->addItem(tr("LNB power off"), static_cast<int>(Polarization::Off));
    lnb->addRow(tr("Polarization:"), m_polarization);
    m_tone = new QComboBox;
    m_tone->addItem(tr("Auto (high band)"), static_cast<int>(ToneMode::Auto));
    m_tone->addItem(tr("Off"), static_cast<int>(ToneMode::Off));
    m_tone->addItem(tr("On"), static_cast<int>(ToneMode::On));
    lnb->addRow(tr("22 kHz tone:"), m_tone);
    m_diseqc = new QComboBox;
    m_diseqc->addItem(tr("None"), -1);
    for (int i = 0; i < 4; ++i)
        m_diseqc->addItem(tr("Committed port %1").arg(QChar('A' + i)), i);
    lnb->addRow(tr("DiSEqC 1.0:"), m_diseqc);
    m_ifLabel = new QLabel;
    lnb->addRow(tr("Tuner IF:"), m_ifLabel);
    root->addWidget(lnbGroup);

    // ---- Multistream ----
    auto *misGroup = new QGroupBox(tr("Multistream / PLS"));
    auto *mis = new QFormLayout(misGroup);
    m_isi = new QSpinBox;
    m_isi->setRange(-1, 255);
    m_isi->setSpecialValueText(tr("Off (all streams)"));
    mis->addRow(tr("Input stream ID:"), m_isi);
    m_plsMode = new QComboBox;
    m_plsMode->addItem(tr("Root"), static_cast<int>(PlsMode::Root));
    m_plsMode->addItem(tr("Gold"), static_cast<int>(PlsMode::Gold));
    m_plsMode->addItem(tr("Combo"), static_cast<int>(PlsMode::Combo));
    mis->addRow(tr("PLS mode:"), m_plsMode);
    m_plsCode = new QSpinBox;
    m_plsCode->setRange(0, 262143);
    mis->addRow(tr("PLS code:"), m_plsCode);
    m_plsLabel = new QLabel;
    mis->addRow(QString(), m_plsLabel);
    root->addWidget(misGroup);

    // ---- Capture ----
    auto *capGroup = new QGroupBox(tr("Capture"));
    auto *cap = new QFormLayout(capGroup);
    m_captureMode = new QComboBox;
    m_captureMode->addItem(tr("Transport stream (TS packets)"), static_cast<int>(CaptureMode::TransportStream));
    m_captureMode->addItem(tr("BBFrames"), static_cast<int>(CaptureMode::BBFrame));
    cap->addRow(tr("Output data:"), m_captureMode);
    m_captureHint = new QLabel;
    m_captureHint->setWordWrap(true);
    QFont small = m_captureHint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    m_captureHint->setFont(small);
    cap->addRow(m_captureHint);
    root->addWidget(capGroup);
    root->addStretch();

    m_editables = {m_device, m_refresh, m_dvbBox, m_replayBox, m_frequency, m_symbolRate, m_delsys,
                   m_modulation, m_lnbType, m_lo1, m_lo2, m_switch, m_polarization, m_tone, m_diseqc,
                   m_isi, m_plsMode, m_plsCode, m_captureMode};

    connect(m_refresh, &QPushButton::clicked, this, &TuningPanel::refreshDevices);
    connect(m_device, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::onDeviceChanged);
    for (QDoubleSpinBox *s : {m_frequency, m_lo1, m_lo2, m_switch})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    for (QComboBox *c : {m_lnbType, m_tone, m_plsMode, m_captureMode, m_polarization})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::updateDerived);
    connect(m_plsCode, qOverload<int>(&QSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    connect(m_isi, qOverload<int>(&QSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    connect(m_captureMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::captureModeChanged);

    refreshDevices();
}

void TuningPanel::refreshDevices()
{
    const QVariant previous = m_device->currentData();
    m_devices = DeviceFactory::enumerate();
#ifdef TBS_HAVE_LINUX_DVB
    // Always offer manual adapter selection (e.g. when the device is busy during the scan)
    DeviceInfo manual;
    manual.kind = DeviceKind::LinuxDvb;
    manual.label = tr("Linux DVB (manual adapter/frontend)");
    m_devices.insert(m_devices.size() - 1, manual);
#endif
    const QSignalBlocker block(m_device);
    m_device->clear();
    for (int i = 0; i < m_devices.size(); ++i)
        m_device->addItem(m_devices[i].label, i);
    int idx = m_device->findData(previous);
    m_device->setCurrentIndex(idx >= 0 ? idx : 0);
    onDeviceChanged();
}

void TuningPanel::onDeviceChanged()
{
    const int i = m_device->currentData().toInt();
    if (i < 0 || i >= m_devices.size())
        return;
    const DeviceInfo &d = m_devices[i];
    const bool dvb = d.kind == DeviceKind::LinuxDvb;
    m_dvbBox->setVisible(dvb);
    m_replayBox->setVisible(!dvb);
    if (dvb && d.adapter >= 0) {
        m_adapter->setValue(d.adapter);
        m_frontend->setValue(d.frontend);
    }
    m_adapter->setEnabled(dvb && d.adapter < 0);
    m_frontend->setEnabled(dvb && d.adapter < 0);
    updateDerived();
}

void TuningPanel::selectDevice(const TunerConfig &cfg)
{
    int best = -1;
    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo &d = m_devices[i];
        if (d.kind != cfg.deviceKind)
            continue;
        if (d.kind == DeviceKind::FileReplay || (d.adapter == cfg.adapter && d.frontend == cfg.frontend)) {
            best = i;
            break;
        }
        if (d.adapter < 0 && best < 0)
            best = i; // manual entry
    }
    if (best >= 0)
        m_device->setCurrentIndex(m_device->findData(best));
    onDeviceChanged();
}

void TuningPanel::setConfig(const TunerConfig &cfg)
{
    selectDevice(cfg);
    m_adapter->setValue(cfg.adapter);
    m_frontend->setValue(cfg.frontend);
    m_demux->setValue(cfg.demux);
    m_replayFile->setText(cfg.replayFile);
    m_replayRate->setValue(cfg.replayRateMbps);
    m_replayLoop->setChecked(cfg.replayLoop);
    m_frequency->setValue(cfg.frequencyMHz);
    m_symbolRate->setValue(cfg.symbolRateKsps);
    m_delsys->setCurrentIndex(m_delsys->findData(static_cast<int>(cfg.deliverySystem)));
    m_modulation->setCurrentIndex(m_modulation->findData(static_cast<int>(cfg.modulation)));
    m_lnbType->setCurrentIndex(m_lnbType->findData(static_cast<int>(cfg.lnbType)));
    m_lo1->setValue(cfg.lnbLo1MHz);
    m_lo2->setValue(cfg.lnbLo2MHz);
    m_switch->setValue(cfg.lnbSwitchMHz);
    m_polarization->setCurrentIndex(m_polarization->findData(static_cast<int>(cfg.polarization)));
    m_tone->setCurrentIndex(m_tone->findData(static_cast<int>(cfg.tone)));
    m_diseqc->setCurrentIndex(m_diseqc->findData(cfg.diseqcPort));
    m_isi->setValue(cfg.inputStreamId);
    m_plsMode->setCurrentIndex(m_plsMode->findData(static_cast<int>(cfg.plsMode)));
    m_plsCode->setValue(static_cast<int>(cfg.plsCode));
    m_captureMode->setCurrentIndex(m_captureMode->findData(static_cast<int>(cfg.captureMode)));
    updateDerived();
}

void TuningPanel::applyTo(TunerConfig &cfg) const
{
    const int i = m_device->currentData().toInt();
    cfg.deviceKind = (i >= 0 && i < m_devices.size()) ? m_devices[i].kind : DeviceKind::FileReplay;
    cfg.adapter = m_adapter->value();
    cfg.frontend = m_frontend->value();
    cfg.demux = m_demux->value();
    cfg.replayFile = m_replayFile->text();
    cfg.replayRateMbps = m_replayRate->value();
    cfg.replayLoop = m_replayLoop->isChecked();
    cfg.frequencyMHz = m_frequency->value();
    cfg.symbolRateKsps = m_symbolRate->value();
    cfg.deliverySystem = static_cast<DeliverySystem>(m_delsys->currentData().toInt());
    cfg.modulation = static_cast<ModulationHint>(m_modulation->currentData().toInt());
    cfg.lnbType = static_cast<LnbType>(m_lnbType->currentData().toInt());
    cfg.lnbLo1MHz = m_lo1->value();
    cfg.lnbLo2MHz = m_lo2->value();
    cfg.lnbSwitchMHz = m_switch->value();
    cfg.polarization = static_cast<Polarization>(m_polarization->currentData().toInt());
    cfg.tone = static_cast<ToneMode>(m_tone->currentData().toInt());
    cfg.diseqcPort = m_diseqc->currentData().toInt();
    cfg.inputStreamId = m_isi->value();
    cfg.plsMode = static_cast<PlsMode>(m_plsMode->currentData().toInt());
    cfg.plsCode = static_cast<uint32_t>(m_plsCode->value());
    cfg.captureMode = static_cast<CaptureMode>(m_captureMode->currentData().toInt());
}

void TuningPanel::setEditable(bool editable)
{
    for (QWidget *w : m_editables)
        w->setEnabled(editable);
    if (editable)
        onDeviceChanged();
}

void TuningPanel::updateDerived()
{
    const auto lnb = static_cast<LnbType>(m_lnbType->currentData().toInt());
    m_lo1->setEnabled(lnb != LnbType::Direct);
    m_lo2->setEnabled(lnb == LnbType::Universal);
    m_switch->setEnabled(lnb == LnbType::Universal);

    TunerConfig cfg;
    applyTo(cfg);
    const double ifMHz = cfg.intermediateFrequencyKHz() / 1000.0;
    const bool inRange = ifMHz >= 950.0 && ifMHz <= 2150.0;
    m_ifLabel->setText(QStringLiteral("<b>%1 MHz</b>, %2 band, tone %3%4")
                           .arg(ifMHz, 0, 'f', 3)
                           .arg(cfg.isHighBand() ? tr("high") : tr("low"))
                           .arg(cfg.toneOn() ? tr("on") : tr("off"))
                           .arg(inRange ? QString() : tr(" <span style='color:#c33'>(outside 950-2150 MHz)</span>")));

    QString pls;
    if (cfg.plsMode == PlsMode::Root)
        pls = tr("Gold code %1").arg(PlsUtil::rootToGold(cfg.plsCode));
    else
        pls = tr("Root code %1").arg(PlsUtil::goldToRoot(cfg.plsCode));
    pls += tr(", DTV_STREAM_ID 0x%1").arg(cfg.dvbStreamId(), 8, 16, QLatin1Char('0'));
    m_plsLabel->setText(pls);

    if (cfg.captureMode == CaptureMode::BBFrame) {
        m_captureHint->setText(tr("BBFrames need the TBS driver loaded with <tt>options stid135 bbframe=1</tt>. "
                                  "The demodulator then forwards Generic Stream/GSE carriers as BBFrames inside "
                                  "pseudo-TS (PID 0x010E); TS carriers are still delivered as TS."));
    } else {
        m_captureHint->setText(tr("The complete transport stream (all PIDs) is captured."));
    }
}
