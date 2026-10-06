#pragma once

#include "core/DeviceFactory.h"
#include "core/TunerConfig.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

class TuningPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TuningPanel(QWidget *parent = nullptr);

    void setConfig(const TunerConfig &cfg);
    void applyTo(TunerConfig &cfg) const;
    void setEditable(bool editable);

public slots:
    void refreshDevices();

signals:
    void captureModeChanged();

private slots:
    void onDeviceChanged();
    void updateDerived();

private:
    void selectDevice(const TunerConfig &cfg);

    QList<DeviceInfo> m_devices;

    QComboBox *m_device = nullptr;
    QPushButton *m_refresh = nullptr;
    QWidget *m_dvbBox = nullptr;
    QSpinBox *m_adapter = nullptr;
    QSpinBox *m_frontend = nullptr;
    QSpinBox *m_demux = nullptr;
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

    QList<QWidget *> m_editables;
};
