#pragma once

#include "core/TunerConfig.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget *parent = nullptr);

    void setConfig(const TunerConfig &cfg);
    void applyTo(TunerConfig &cfg) const;
    void setEditable(bool editable);
    void setCaptureMode(CaptureMode mode);

private:
    QGroupBox *m_fileGroup = nullptr;
    QLineEdit *m_filePath = nullptr;
    QCheckBox *m_timestamp = nullptr;
    QComboBox *m_bbFormat = nullptr;
    QLabel *m_bbFormatLabel = nullptr;

    QGroupBox *m_zmqGroup = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QComboBox *m_socketType = nullptr;
    QComboBox *m_bindMode = nullptr;
    QLineEdit *m_topic = nullptr;
    QCheckBox *m_header = nullptr;
    QSpinBox *m_packetsPerMsg = nullptr;
    QSpinBox *m_hwm = nullptr;

    QCheckBox *m_video = nullptr;
};
