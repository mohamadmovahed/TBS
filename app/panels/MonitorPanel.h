#pragma once

#include <tbsdvb/Monitor.h>

#include <QDialog>
#include <QWidget>

class HistoryChart;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;

// Alarms, alarm events and the receiver history (C/N, level, bitrate, errors).
class MonitorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPanel(tbs::Monitor *monitor, QWidget *parent = nullptr);

    // Refresh from the monitor (call about once a second)
    void refresh();

signals:
    void configChanged(const tbs::MonitorConfig &cfg);

private:
    void refreshChart();
    void exportCsv();
    void editSettings();

    tbs::Monitor *m_monitor;
    QTreeWidget *m_alarms = nullptr;
    QTreeWidget *m_events = nullptr;
    QComboBox *m_metric = nullptr;
    QComboBox *m_range = nullptr;
    HistoryChart *m_chart = nullptr;
    QLabel *m_chartTitle = nullptr;
    size_t m_eventCount = 0;
};

// Thresholds and the alarm command
class AlarmSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    AlarmSettingsDialog(const tbs::MonitorConfig &cfg, QWidget *parent = nullptr);
    tbs::MonitorConfig config() const;

private:
    tbs::MonitorConfig m_cfg;
    QCheckBox *m_noLock, *m_lowCnr, *m_lowLevel, *m_cc, *m_crc, *m_lowRate, *m_noData, *m_drops, *m_output;
    QSpinBox *m_noLockHold, *m_cnrHold, *m_levelHold, *m_ccMax, *m_crcMax, *m_window, *m_rateHold, *m_noDataHold;
    QDoubleSpinBox *m_cnrMin, *m_levelMin, *m_rateMin;
    QLineEdit *m_command;
};
