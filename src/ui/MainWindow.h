#pragma once

#include "core/TunerConfig.h"

#include <QMainWindow>

class OutputPanel;
class QAction;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class ReceiverEngine;
class StatusPanel;
class TuningPanel;
class VideoDecoder;
class VideoWidget;
struct StreamStats;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void startCapture();
    void stopCapture();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStarted();
    void onStopped();
    void onFailed(const QString &message);
    void appendLog(const QString &message);
    void updatePidTable(const StreamStats &stats);
    void showAbout();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    TunerConfig currentConfig() const;
    void setRunningUi(bool running);

    TunerConfig m_config;
    ReceiverEngine *m_engine = nullptr;
    VideoDecoder *m_decoder = nullptr;

    TuningPanel *m_tuning = nullptr;
    OutputPanel *m_output = nullptr;
    StatusPanel *m_status = nullptr;
    VideoWidget *m_video = nullptr;
    QTableWidget *m_pidTable = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_rateLabel = nullptr;
};
