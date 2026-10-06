#pragma once

#include <tbsdvb/Types.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class ToggleSwitch;

class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget *parent = nullptr);

    void setConfig(const tbs::ReceiverConfig &cfg, bool videoPreview);
    void applyTo(tbs::ReceiverConfig &cfg) const;
    bool videoPreview() const;
    void setEditable(bool editable);
    void setCaptureMode(tbs::CaptureMode mode);

signals:
    void videoPreviewToggled(bool on);

private:
    void updateEnabled();

    bool m_editable = true;
    tbs::CaptureMode m_mode = tbs::CaptureMode::TransportStream;

    ToggleSwitch *m_fileOn = nullptr;
    QWidget *m_fileBox = nullptr;
    QLineEdit *m_filePath = nullptr;
    QCheckBox *m_timestamp = nullptr;
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
};
