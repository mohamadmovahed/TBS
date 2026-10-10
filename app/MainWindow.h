#pragma once

#include "ReceiverController.h"

#include <QMainWindow>

class IpPanel;
class LockChain;
class QTimer;
class ServicesPanel;
class MetricTile;
class OutputPanel;
class OverviewPanel;
class PidTable;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;
class StatusPill;
class TuningPanel;
class VideoDecoder;
class VideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void startCapture();
    void stopCapture();
    void toggleCapture();
    void applyTuning();
    // Selects a details tab by name: overview, services, ip, pids, log
    void showTab(const QString &name);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStateChanged(int state, const QString &error);
    void onStatus(const tbs::FrontendStatus &st);
    void onStats(const tbs::StreamStats &st);
    void appendLog(const QString &message);
    void toggleTheme();
    void setPreview(bool on);
    void runDiagnostics();
    void refreshProcessing();

private:
    QWidget *buildHeader();
    QWidget *buildSidebar();
    QWidget *buildMain();
    void loadSettings();
    void saveSettings();
    tbs::ReceiverConfig currentConfig() const;
    void setRunningUi(bool running);
    void refreshIcons();
    void resetMetrics();

    ReceiverController *m_controller = nullptr;
    VideoDecoder *m_decoder = nullptr;
    tbs::ReceiverConfig m_config;
    bool m_running = false;
    bool m_hadError = false;
    bool m_tuningDirty = false;
    uint64_t m_lastCc = 0;

    // Header
    QLabel *m_logo = nullptr;
    QLabel *m_subtitle = nullptr;
    StatusPill *m_pill = nullptr;
    QToolButton *m_themeButton = nullptr;
    QPushButton *m_startButton = nullptr;

    // Sidebar
    QToolButton *m_tuningTab = nullptr;
    QToolButton *m_outputTab = nullptr;
    QStackedWidget *m_stack = nullptr;
    TuningPanel *m_tuning = nullptr;
    OutputPanel *m_output = nullptr;
    QPushButton *m_applyButton = nullptr;

    // Main area
    VideoWidget *m_video = nullptr;
    LockChain *m_chain = nullptr;
    MetricTile *m_levelTile = nullptr;
    MetricTile *m_cnrTile = nullptr;
    MetricTile *m_rateTile = nullptr;
    MetricTile *m_errorTile = nullptr;
    OverviewPanel *m_overview = nullptr;
    PidTable *m_pids = nullptr;
    QPlainTextEdit *m_log = nullptr;
    class QTabWidget *m_tabs = nullptr;
    QWidget *m_logTab = nullptr;
    ServicesPanel *m_services = nullptr;
    IpPanel *m_ip = nullptr;
    std::shared_ptr<tbs::bb::Processor> m_processor;
    QTimer *m_procTimer = nullptr;
    QLabel *m_statusText = nullptr;
};
