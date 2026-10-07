#pragma once

#include <tbsdvb/Types.h>

#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class TuningPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TuningPanel(QWidget *parent = nullptr);

    void setConfig(const tbs::ReceiverConfig &cfg);
    void applyTo(tbs::ReceiverConfig &cfg) const;
    tbs::TuningParams tuning() const;
    // While running only the tuning fields stay editable (they can be applied with retune)
    void setRunning(bool running);

public slots:
    void refreshDevices();

signals:
    void captureModeChanged();
    void tuningEdited();
    void diagnoseRequested();

private slots:
    void onDeviceChanged();
    void updateDerived();

private:
    void selectDevice(const tbs::DeviceSelection &sel);
    tbs::DeviceKind currentKind() const;

    std::vector<tbs::DeviceInfo> m_devices;
    bool m_running = false;

    QComboBox *m_device = nullptr;
    QPushButton *m_rescan = nullptr;
    QPushButton *m_diagnose = nullptr;
    QWidget *m_dvbBox = nullptr;
    QSpinBox *m_adapter = nullptr;
    QSpinBox *m_frontend = nullptr;
    QSpinBox *m_demux = nullptr;
    QWidget *m_bdaBox = nullptr;
    QSpinBox *m_bdaIndex = nullptr;
    QWidget *m_replayBox = nullptr;
    QLineEdit *m_replayFile = nullptr;
    QDoubleSpinBox *m_replayRate = nullptr;
    QCheckBox *m_replayLoop = nullptr;

    QDoubleSpinBox *m_frequency = nullptr;
    QDoubleSpinBox *m_symbolRate = nullptr;
    QComboBox *m_delsys = nullptr;
    QComboBox *m_modulation = nullptr;

    QComboBox *m_lnbType = nullptr;
    QDoubleSpinBox *m_lo1 = nullptr;
    QDoubleSpinBox *m_lo2 = nullptr;
    QDoubleSpinBox *m_switch = nullptr;
    QComboBox *m_polarization = nullptr;
    QComboBox *m_tone = nullptr;
    QComboBox *m_diseqc = nullptr;
    QLabel *m_ifLabel = nullptr;

    QSpinBox *m_isi = nullptr;
    QComboBox *m_plsMode = nullptr;
    QSpinBox *m_plsCode = nullptr;
    QLabel *m_plsLabel = nullptr;

    QComboBox *m_captureMode = nullptr;
    QLabel *m_captureHint = nullptr;

    QList<QWidget *> m_deviceWidgets;
};
