#include "TuningPanel.h"

#include "../Theme.h"
#include "FormHelpers.h"

#include <tbsdvb/Device.h>
#include <tbsdvb/PlsUtil.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace tbs;

namespace {
QDoubleSpinBox *makeMHz(double min, double max, int decimals = 3)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSuffix(QStringLiteral(" MHz"));
    s->setAccelerated(true);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}
QSpinBox *makeSpin(int min, int max)
{
    auto *s = new QSpinBox;
    s->setRange(min, max);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}
} // namespace

TuningPanel::TuningPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 6, 18, 18);
    root->setSpacing(4);

    // ---- Receiver ----
    root->addWidget(Theme::sectionTitle(tr("Receiver")));
    auto *devRow = new QHBoxLayout;
    m_device = new QComboBox;
    m_device->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_device->setMinimumContentsLength(20);
    m_rescan = new QPushButton;
    m_rescan->setObjectName(QStringLiteral("Ghost"));
    m_rescan->setIcon(Theme::icon(QStringLiteral("refresh"), Theme::tokens().ink2));
    m_rescan->setToolTip(tr("Rescan receivers"));
    m_diagnose = new QPushButton(tr("Diagnose"));
    m_diagnose->setObjectName(QStringLiteral("Ghost"));
    m_diagnose->setToolTip(tr("Check the driver and device setup for the selected receiver"));
    devRow->addWidget(m_device, 1);
    devRow->addWidget(m_rescan);
    devRow->addWidget(m_diagnose);
    root->addLayout(devRow);
    connect(m_diagnose, &QPushButton::clicked, this, &TuningPanel::diagnoseRequested);

    m_dvbBox = new QWidget;
    auto *dvbForm = makeForm();
    m_dvbBox->setLayout(dvbForm);
    auto *dvbRow = new QHBoxLayout;
    m_adapter = makeSpin(0, 63);
    m_frontend = makeSpin(0, 15);
    m_demux = makeSpin(0, 15);
    dvbRow->addWidget(m_adapter);
    dvbRow->addWidget(keyLabel(tr("FE")));
    dvbRow->addWidget(m_frontend);
    dvbRow->addWidget(keyLabel(tr("Demux")));
    dvbRow->addWidget(m_demux);
    dvbForm->addRow(keyLabel(tr("Adapter")), dvbRow);
    root->addWidget(m_dvbBox);

    m_bdaBox = new QWidget;
    auto *bdaForm = makeForm();
    m_bdaBox->setLayout(bdaForm);
    m_bdaIndex = makeSpin(0, 63);
    bdaForm->addRow(keyLabel(tr("BDA tuner #")), m_bdaIndex);
    root->addWidget(m_bdaBox);

    m_replayBox = new QWidget;
    auto *replayForm = makeForm();
    m_replayBox->setLayout(replayForm);
    auto *fileRow = new QHBoxLayout;
    m_replayFile = new QLineEdit;
    m_replayFile->setPlaceholderText(tr("Recording (.ts, pseudo-TS)"));
    auto *browse = new QPushButton;
    browse->setObjectName(QStringLiteral("Ghost"));
    browse->setIcon(Theme::icon(QStringLiteral("folder"), Theme::tokens().ink2));
    browse->setToolTip(tr("Browse"));
    fileRow->addWidget(m_replayFile, 1);
    fileRow->addWidget(browse);
    replayForm->addRow(keyLabel(tr("File")), fileRow);
    m_replayRate = new QDoubleSpinBox;
    m_replayRate->setRange(0, 1000);
    m_replayRate->setDecimals(2);
    m_replayRate->setSuffix(QStringLiteral(" Mbit/s"));
    m_replayRate->setSpecialValueText(tr("Unthrottled"));
    m_replayRate->setButtonSymbols(QAbstractSpinBox::NoButtons);
    replayForm->addRow(keyLabel(tr("Rate")), m_replayRate);
    m_replayLoop = new QCheckBox(tr("Loop at end of file"));
    replayForm->addRow(QString(), m_replayLoop);
    root->addWidget(m_replayBox);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Select recording"), m_replayFile->text(),
                                                       tr("Transport streams (*.ts *.trp *.mpg *.bin);;All files (*)"));
        if (!f.isEmpty())
            m_replayFile->setText(f);
    });

    // ---- Carrier ----
    root->addWidget(Theme::sectionTitle(tr("Carrier")));
    auto *carrier = makeForm();
    m_frequency = makeMHz(0, 30000);
    carrier->addRow(keyLabel(tr("Frequency")), m_frequency);
    m_symbolRate = new QDoubleSpinBox;
    m_symbolRate->setRange(100, 520000);
    m_symbolRate->setDecimals(3);
    m_symbolRate->setSuffix(QStringLiteral(" kS/s"));
    m_symbolRate->setButtonSymbols(QAbstractSpinBox::NoButtons);
    carrier->addRow(keyLabel(tr("Symbol rate")), m_symbolRate);
    m_delsys = new QComboBox;
    m_delsys->addItem(tr("DVB-S2 / S2X"), static_cast<int>(DeliverySystem::DvbS2));
    m_delsys->addItem(tr("DVB-S"), static_cast<int>(DeliverySystem::DvbS));
    carrier->addRow(keyLabel(tr("Standard")), m_delsys);
    m_modulation = new QComboBox;
    m_modulation->addItem(tr("Auto detect"), static_cast<int>(ModulationHint::Auto));
    m_modulation->addItem(QStringLiteral("QPSK"), static_cast<int>(ModulationHint::Qpsk));
    m_modulation->addItem(QStringLiteral("8PSK"), static_cast<int>(ModulationHint::Psk8));
    m_modulation->addItem(QStringLiteral("16APSK"), static_cast<int>(ModulationHint::Apsk16));
    m_modulation->addItem(QStringLiteral("32APSK"), static_cast<int>(ModulationHint::Apsk32));
    carrier->addRow(keyLabel(tr("Modulation")), m_modulation);
    root->addLayout(carrier);

    // ---- LNB ----
    root->addWidget(Theme::sectionTitle(tr("LNB & switch")));
    auto *lnb = makeForm();
    m_lnbType = new QComboBox;
    m_lnbType->addItem(tr("Universal Ku (2 LO)"), static_cast<int>(LnbType::Universal));
    m_lnbType->addItem(tr("Single LO (C band, Ku…)"), static_cast<int>(LnbType::SingleLo));
    m_lnbType->addItem(tr("None — L-band input"), static_cast<int>(LnbType::Direct));
    lnb->addRow(keyLabel(tr("LNB")), m_lnbType);
    m_lo1 = makeMHz(0, 30000);
    lnb->addRow(keyLabel(tr("LO low")), m_lo1);
    m_lo2 = makeMHz(0, 30000);
    lnb->addRow(keyLabel(tr("LO high")), m_lo2);
    m_switch = makeMHz(0, 30000);
    lnb->addRow(keyLabel(tr("Band switch")), m_switch);
    m_polarization = new QComboBox;
    m_polarization->addItem(tr("Horizontal / Left · 18 V"), static_cast<int>(Polarization::HorizontalLeft18V));
    m_polarization->addItem(tr("Vertical / Right · 13 V"), static_cast<int>(Polarization::VerticalRight13V));
    m_polarization->addItem(tr("LNB power off"), static_cast<int>(Polarization::Off));
    lnb->addRow(keyLabel(tr("Polarization")), m_polarization);
    m_tone = new QComboBox;
    m_tone->addItem(tr("Auto (high band)"), static_cast<int>(ToneMode::Auto));
    m_tone->addItem(tr("Off"), static_cast<int>(ToneMode::Off));
    m_tone->addItem(tr("On"), static_cast<int>(ToneMode::On));
    lnb->addRow(keyLabel(tr("22 kHz tone")), m_tone);
    m_diseqc = new QComboBox;
    m_diseqc->addItem(tr("None"), -1);
    for (int i = 0; i < 4; ++i)
        m_diseqc->addItem(tr("Committed port %1").arg(QChar('A' + i)), i);
    lnb->addRow(keyLabel(tr("DiSEqC 1.0")), m_diseqc);
    root->addLayout(lnb);
    m_ifLabel = hintLabel();
    root->addWidget(m_ifLabel);

    // ---- Multistream ----
    root->addWidget(Theme::sectionTitle(tr("Multistream & PLS")));
    auto *mis = makeForm();
    m_isi = makeSpin(-1, 255);
    m_isi->setSpecialValueText(tr("Off — all streams"));
    mis->addRow(keyLabel(tr("Input stream ID")), m_isi);
    m_plsMode = new QComboBox;
    m_plsMode->addItem(tr("Gold"), static_cast<int>(PlsMode::Gold));
    m_plsMode->addItem(tr("Root"), static_cast<int>(PlsMode::Root));
    m_plsMode->addItem(tr("Combo"), static_cast<int>(PlsMode::Combo));
    mis->addRow(keyLabel(tr("PLS mode")), m_plsMode);
    m_plsCode = makeSpin(0, 262143);
    mis->addRow(keyLabel(tr("PLS code")), m_plsCode);
    root->addLayout(mis);
    m_plsLabel = hintLabel();
    root->addWidget(m_plsLabel);

    // ---- Capture ----
    root->addWidget(Theme::sectionTitle(tr("Capture")));
    auto *cap = makeForm();
    m_captureMode = new QComboBox;
    m_captureMode->addItem(tr("Transport stream"), static_cast<int>(CaptureMode::TransportStream));
    m_captureMode->addItem(tr("BBFrames"), static_cast<int>(CaptureMode::BBFrame));
    cap->addRow(keyLabel(tr("Output data")), m_captureMode);
    root->addLayout(cap);
    m_captureHint = hintLabel();
    root->addWidget(m_captureHint);
    root->addStretch();

    m_deviceWidgets = {m_device, m_rescan, m_dvbBox, m_bdaBox, m_replayBox, m_captureMode};

    connect(m_rescan, &QPushButton::clicked, this, &TuningPanel::refreshDevices);
    connect(m_device, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::onDeviceChanged);
    for (QDoubleSpinBox *s : {m_frequency, m_lo1, m_lo2, m_switch, m_symbolRate})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    for (QComboBox *c : {m_lnbType, m_tone, m_plsMode, m_captureMode, m_polarization, m_delsys, m_modulation, m_diseqc})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::updateDerived);
    connect(m_plsCode, qOverload<int>(&QSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    connect(m_isi, qOverload<int>(&QSpinBox::valueChanged), this, &TuningPanel::updateDerived);
    connect(m_captureMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &TuningPanel::captureModeChanged);

    refreshDevices();
}

void TuningPanel::refreshDevices()
{
    DeviceSelection previous;
    tbs::ReceiverConfig tmp;
    applyTo(tmp);
    previous = tmp.device;

    m_devices = Devices::enumerate(true);
    // Manual entries for devices that are busy or not yet present
    std::vector<DeviceInfo> manual;
    if (Devices::isSupported(DeviceKind::LinuxDvb)) {
        DeviceInfo d;
        d.kind = DeviceKind::LinuxDvb;
        d.name = tr("Linux DVB — manual adapter").toStdString();
        manual.push_back(d);
    }
    if (Devices::isSupported(DeviceKind::WindowsBda)) {
        DeviceInfo d;
        d.kind = DeviceKind::WindowsBda;
        d.name = tr("BDA tuner — manual index").toStdString();
        manual.push_back(d);
    }
    m_devices.insert(m_devices.end() - 1, manual.begin(), manual.end());

    const QSignalBlocker block(m_device);
    m_device->clear();
    for (size_t i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo &d = m_devices[i];
        const bool isManual = d.adapter < 0 && d.index < 0 && d.kind != DeviceKind::FileReplay;
        m_device->addItem(QString::fromStdString(isManual || d.kind == DeviceKind::FileReplay ? d.name : d.label()),
                          static_cast<int>(i));
    }
    selectDevice(previous);
}

tbs::DeviceKind TuningPanel::currentKind() const
{
    const int i = m_device->currentData().toInt();
    return (i >= 0 && i < static_cast<int>(m_devices.size())) ? m_devices[static_cast<size_t>(i)].kind : DeviceKind::FileReplay;
}

void TuningPanel::onDeviceChanged()
{
    const int i = m_device->currentData().toInt();
    if (i < 0 || i >= static_cast<int>(m_devices.size()))
        return;
    const DeviceInfo &d = m_devices[static_cast<size_t>(i)];
    m_dvbBox->setVisible(d.kind == DeviceKind::LinuxDvb);
    m_bdaBox->setVisible(d.kind == DeviceKind::WindowsBda && d.index < 0);
    m_replayBox->setVisible(d.kind == DeviceKind::FileReplay);
    if (d.kind == DeviceKind::LinuxDvb && d.adapter >= 0) {
        m_adapter->setValue(d.adapter);
        m_frontend->setValue(d.frontend);
    }
    if (d.kind == DeviceKind::WindowsBda && d.index >= 0)
        m_bdaIndex->setValue(d.index);
    const bool manualLinux = d.kind == DeviceKind::LinuxDvb && d.adapter < 0;
    m_adapter->setEnabled(manualLinux && !m_running);
    m_frontend->setEnabled(manualLinux && !m_running);
    updateDerived();
}

void TuningPanel::selectDevice(const DeviceSelection &sel)
{
    int best = -1;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo &d = m_devices[i];
        if (d.kind != sel.kind)
            continue;
        const bool match = d.kind == DeviceKind::FileReplay
            || (d.kind == DeviceKind::LinuxDvb && d.adapter == sel.adapter && d.frontend == sel.frontend)
            || (d.kind == DeviceKind::WindowsBda && d.index >= 0
                && (sel.bdaDevicePath.empty() ? d.index == sel.bdaIndex : d.path == sel.bdaDevicePath));
        if (match) {
            best = static_cast<int>(i);
            break;
        }
        if (d.adapter < 0 && d.index < 0 && best < 0)
            best = static_cast<int>(i); // manual entry of the same kind
    }
    if (best >= 0)
        m_device->setCurrentIndex(m_device->findData(best));
    onDeviceChanged();
}

void TuningPanel::setConfig(const tbs::ReceiverConfig &cfg)
{
    const DeviceSelection &dev = cfg.device;
    m_adapter->setValue(dev.adapter);
    m_frontend->setValue(dev.frontend);
    m_demux->setValue(dev.demux);
    m_bdaIndex->setValue(dev.bdaIndex);
    m_replayFile->setText(QString::fromStdString(dev.replayFile));
    m_replayRate->setValue(dev.replayRateMbps);
    m_replayLoop->setChecked(dev.replayLoop);
    selectDevice(dev);

    const TuningParams &t = cfg.tuning;
    m_frequency->setValue(t.frequencyMHz);
    m_symbolRate->setValue(t.symbolRateKsps);
    m_delsys->setCurrentIndex(m_delsys->findData(static_cast<int>(t.deliverySystem)));
    m_modulation->setCurrentIndex(m_modulation->findData(static_cast<int>(t.modulation)));
    m_lnbType->setCurrentIndex(m_lnbType->findData(static_cast<int>(t.lnbType)));
    m_lo1->setValue(t.lnbLo1MHz);
    m_lo2->setValue(t.lnbLo2MHz);
    m_switch->setValue(t.lnbSwitchMHz);
    m_polarization->setCurrentIndex(m_polarization->findData(static_cast<int>(t.polarization)));
    m_tone->setCurrentIndex(m_tone->findData(static_cast<int>(t.tone)));
    m_diseqc->setCurrentIndex(m_diseqc->findData(t.diseqcPort));
    m_isi->setValue(t.inputStreamId);
    m_plsMode->setCurrentIndex(m_plsMode->findData(static_cast<int>(t.plsMode)));
    m_plsCode->setValue(static_cast<int>(t.plsCode));
    m_captureMode->setCurrentIndex(m_captureMode->findData(static_cast<int>(cfg.capture)));
    updateDerived();
}

tbs::TuningParams TuningPanel::tuning() const
{
    TuningParams t;
    t.frequencyMHz = m_frequency->value();
    t.symbolRateKsps = m_symbolRate->value();
    t.deliverySystem = static_cast<DeliverySystem>(m_delsys->currentData().toInt());
    t.modulation = static_cast<ModulationHint>(m_modulation->currentData().toInt());
    t.lnbType = static_cast<LnbType>(m_lnbType->currentData().toInt());
    t.lnbLo1MHz = m_lo1->value();
    t.lnbLo2MHz = m_lo2->value();
    t.lnbSwitchMHz = m_switch->value();
    t.polarization = static_cast<Polarization>(m_polarization->currentData().toInt());
    t.tone = static_cast<ToneMode>(m_tone->currentData().toInt());
    t.diseqcPort = m_diseqc->currentData().toInt();
    t.inputStreamId = m_isi->value();
    t.plsMode = static_cast<PlsMode>(m_plsMode->currentData().toInt());
    t.plsCode = static_cast<uint32_t>(m_plsCode->value());
    return t;
}

void TuningPanel::applyTo(tbs::ReceiverConfig &cfg) const
{
    DeviceSelection &dev = cfg.device;
    const int i = m_device->currentData().toInt();
    if (i >= 0 && i < static_cast<int>(m_devices.size()))
        m_devices[static_cast<size_t>(i)].applyTo(dev);
    dev.kind = currentKind();
    dev.adapter = m_adapter->value();
    dev.frontend = m_frontend->value();
    dev.demux = m_demux->value();
    if (dev.kind == DeviceKind::WindowsBda && (i < 0 || m_devices[static_cast<size_t>(i)].index < 0)) {
        dev.bdaIndex = m_bdaIndex->value();
        dev.bdaDevicePath.clear();
    }
    dev.replayFile = m_replayFile->text().toStdString();
    dev.replayRateMbps = m_replayRate->value();
    dev.replayLoop = m_replayLoop->isChecked();
    cfg.tuning = tuning();
    cfg.capture = static_cast<CaptureMode>(m_captureMode->currentData().toInt());
}

void TuningPanel::setRunning(bool running)
{
    m_running = running;
    for (QWidget *w : m_deviceWidgets)
        w->setEnabled(!running);
    onDeviceChanged();
}

void TuningPanel::updateDerived()
{
    const auto lnb = static_cast<LnbType>(m_lnbType->currentData().toInt());
    m_lo1->setEnabled(lnb != LnbType::Direct);
    m_lo2->setEnabled(lnb == LnbType::Universal);
    m_switch->setEnabled(lnb == LnbType::Universal);

    const TuningParams t = tuning();
    const double ifMHz = t.intermediateFrequencyKHz() / 1000.0;
    const bool inRange = ifMHz >= 950.0 && ifMHz <= 2150.0;
    const Theme::Tokens &tok = Theme::tokens();
    m_ifLabel->setText(tr("Tuner IF <b style='color:%1'>%2 MHz</b> · %3 band · tone %4%5")
                           .arg(tok.ink.name())
                           .arg(ifMHz, 0, 'f', 3)
                           .arg(t.isHighBand() ? tr("high") : tr("low"))
                           .arg(t.toneOn() ? tr("on") : tr("off"))
                           .arg(inRange ? QString()
                                        : tr(" · <span style='color:%1'>⚠ outside 950–2150 MHz</span>").arg(tok.critical.name())));

    QString pls = t.plsMode == PlsMode::Root ? tr("Gold code %1").arg(PlsUtil::rootToGold(t.plsCode))
                                             : tr("Root code %1").arg(PlsUtil::goldToRoot(t.plsCode));
    pls += tr(" · stream id 0x%1").arg(t.dvbStreamId(), 8, 16, QLatin1Char('0'));
    m_plsLabel->setText(pls);

    if (static_cast<CaptureMode>(m_captureMode->currentData().toInt()) == CaptureMode::BBFrame) {
        m_captureHint->setText(tr("BBFrames come wrapped in pseudo-TS (PID 0x010E/0x0118). On Linux load the driver with "
                                  "<tt>options stid135 bbframe=1</tt>. Only Generic Stream / GSE carriers are delivered "
                                  "as BBFrames; TS carriers stay TS."));
    } else {
        m_captureHint->setText(tr("The complete transport stream (all PIDs) is captured."));
    }
    // Only edits made by the user (signal-driven) mark the tuning as changed
    if (sender())
        emit tuningEdited();
}
