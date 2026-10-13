#pragma once

#include "ReceiverController.h"

#include <tbsdvb/Monitor.h>

#include <memory>

#include <QMainWindow>
#include <QMap>

class EpgPanel;
class MonitorPanel;
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
class AudioOutput;
class AvSync;
class QComboBox;
class QSlider;
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
    // Selects a details tab by name: overview, services, epg, ip, pids, log
    void showTab(const QString &name);
    void showSidebar(const QString &name); // tuning or outputs

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
    // Preview selection: input stream, service (0 = first with video), audio PID (-1 default, 0 off)
    void selectPreview(int isi, int programNumber, int audioPid);

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
    void updatePreviewChoices(const tbs::bb::ProcessorSnapshot &snap);
    void applyPreviewSelection();

    ReceiverController *m_controller = nullptr;
    VideoDecoder *m_decoder = nullptr;
    AudioOutput *m_audio = nullptr;
    AvSync *m_avsync = nullptr;
    bool m_audioStarted = false;
    int m_previewIsi = -1;
    int m_previewProgram = 0;
    int m_previewAudioPid = -1;
    size_t m_previewChoicesSig = 0;
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
    QComboBox *m_previewService = nullptr;
    QComboBox *m_previewAudio = nullptr;
    QToolButton *m_muteButton = nullptr;
    QSlider *m_volume = nullptr;
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
    EpgPanel *m_epg = nullptr;
    MonitorPanel *m_monitorPanel = nullptr;
    std::unique_ptr<tbs::Monitor> m_monitor;
    tbs::FrontendStatus m_lastStatus;
    void updateAlarmIndicator();
    struct EpgState
    {
        uint64_t version = 0;
        qint64 fetchedMs = 0;
    };
    QMap<int, EpgState> m_epgState;
    IpPanel *m_ip = nullptr;
    std::shared_ptr<tbs::bb::Processor> m_processor;
    QTimer *m_procTimer = nullptr;
    QLabel *m_statusText = nullptr;
};
