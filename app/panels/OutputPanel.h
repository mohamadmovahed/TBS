#pragma once

#include "NetOutputDialog.h"

#include <tbsbbproc/Processor.h>
#include <tbsdvb/Status.h>
#include <tbsdvb/Types.h>

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class ToggleSwitch;

class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget *parent = nullptr);

    void setConfig(const tbs::ReceiverConfig &cfg, bool videoPreview);
    void applyTo(tbs::ReceiverConfig &cfg) const;
    bool videoPreview() const;
    void setProcessing(const tbs::bb::ProcessorConfig &cfg);
    tbs::bb::ProcessorConfig processing() const;
    void setEditable(bool editable);
    void setCaptureMode(tbs::CaptureMode mode);
    // Services offered when editing a network output (from the table analysis)
    void setServices(const QVector<ServiceChoice> &services);
    // Live state of the enabled network outputs, in configuration order
    void setNetworkStatus(const std::vector<tbs::NetOutputStatus> &status);

signals:
    void videoPreviewToggled(bool on);

private:
    struct NetRow
    {
        tbs::TsNetworkOutput output;
        QWidget *widget = nullptr;
        ToggleSwitch *on = nullptr;
        QLabel *title = nullptr;
        QLabel *detail = nullptr;
        QLabel *status = nullptr;
        QPushButton *edit = nullptr;
        QPushButton *remove = nullptr;
    };

    void updateEnabled();
    void addNetRow(const tbs::TsNetworkOutput &o);
    void refreshNetRow(NetRow &row);
    void editNetRow(QWidget *rowWidget);
    void removeNetRow(QWidget *rowWidget);
    void clearNetRows();

    bool m_editable = true;
    tbs::CaptureMode m_mode = tbs::CaptureMode::TransportStream;

    ToggleSwitch *m_fileOn = nullptr;
    QWidget *m_fileBox = nullptr;
    QLineEdit *m_filePath = nullptr;
    QCheckBox *m_timestamp = nullptr;
    QSpinBox *m_splitSize = nullptr;
    QSpinBox *m_splitMinutes = nullptr;
    QSpinBox *m_keepFiles = nullptr;
    QSpinBox *m_minFree = nullptr;
    QComboBox *m_bbFormat = nullptr;
    QLabel *m_bbFormatLabel = nullptr;

    ToggleSwitch *m_zmqOn = nullptr;
    QWidget *m_zmqBox = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QComboBox *m_socketType = nullptr;
    QComboBox *m_bindMode = nullptr;
    QLineEdit *m_topic = nullptr;
    QCheckBox *m_header = nullptr;
    QSpinBox *m_packetsPerMsg = nullptr;
    QSpinBox *m_hwm = nullptr;

    ToggleSwitch *m_video = nullptr;

    ToggleSwitch *m_pcapOn = nullptr;
    QWidget *m_pcapBox = nullptr;
    QLineEdit *m_pcapPath = nullptr;
    QComboBox *m_pcapFraming = nullptr;
    QCheckBox *m_pcapNonIp = nullptr;
    QCheckBox *m_pcapTimestamp = nullptr;
    QComboBox *m_pcapFormat = nullptr;
    QComboBox *m_genericDecoder = nullptr;
    ToggleSwitch *m_tunOn = nullptr;
    QWidget *m_tunBox = nullptr;
    QLineEdit *m_tunName = nullptr;
    QComboBox *m_tunMode = nullptr;

    QVBoxLayout *m_netList = nullptr;
    QPushButton *m_netAdd = nullptr;
    QLabel *m_netEmpty = nullptr;
    QVector<NetRow> m_netRows;
    QVector<ServiceChoice> m_services;
};
