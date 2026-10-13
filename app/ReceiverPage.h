#pragma once

#include "ReceiverController.h"

#include <tbsdvb/Monitor.h>

#include <memory>

#include <QWidget>
#include <QMap>

class ConstellationView;
class EpgPanel;
class ScanDialog;
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

// One receiver (tuner): settings sidebar, video, status and detail tabs.
// Several pages run side by side in MainWindow; page 0 keeps the original settings keys.
class ReceiverPage : public QWidget
{
    Q_OBJECT

public:
    explicit ReceiverPage(int index = 0, QWidget *parent = nullptr);

    int index() const { return m_index; }
    QString name() const { return m_name; }
    void setName(const QString &name);
    bool isRunning() const { return m_running; }
    bool isLocked() const;
    size_t activeAlarms() const { return m_monitor->activeAlarms(); }
    // The page on screen: only it is heard
    void setActive(bool active);
    void refreshTheme();
    // Saves the settings and stops the capture (window closing / page removed)
    void shutdown();
    ~ReceiverPage() override;

public slots:
    void startCapture();
    void stopCapture();
    void toggleCapture();
    void applyTuning();
    // Selects a details tab by name: overview, services, epg, monitor, ip, pids, constellation, log
    void showTab(const QString &name);
    void openScanDialog();
    void showSidebar(const QString &name); // tuning or outputs

signals:
    void themeToggleRequested();
    void summaryChanged(); // name, running or lock changed

private slots:
    void onStateChanged(int state, const QString &error);
    void onStatus(const tbs::FrontendStatus &st);
    void onStats(const tbs::StreamStats &st);
    void appendLog(const QString &message);
    void setPreview(bool on);
    void runDiagnostics();
    void refreshProcessing();
    // Preview selection: input stream, service (0 = first with video), audio PID (-1 default, 0 off)
    void selectPreview(int isi, int programNumber, int audioPid);

private:
    QWidget *buildHeader();
    QWidget *buildSidebar();
    QWidget *buildMain();
    void openGroup(class QSettings &s) const;
    QString defaultName() const;
    void loadSettings();
    void saveSettings();
    tbs::ReceiverConfig currentConfig() const;
    void setRunningUi(bool running);
    void refreshIcons();
    void resetMetrics();
    void updatePreviewChoices(const tbs::bb::ProcessorSnapshot &snap);
    void applyPreviewSelection();

    int m_index = 0;
    QString m_prefix; // QSettings group ("" for the first receiver)
    QString m_name;
    bool m_active = true;
    QLabel *m_title = nullptr;
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

    // Scan dialog and constellation tab
    void tuneTo(const tbs::TuningParams &carrier);
    void updateConstellation();
    ScanDialog *m_scanDialog = nullptr;
    ConstellationView *m_constellation = nullptr;
    QWidget *m_constellationTab = nullptr;
    QLabel *m_constellationHint = nullptr;
    QTimer *m_iqTimer = nullptr;
};
