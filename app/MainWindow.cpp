#include "MainWindow.h"

#include "SettingsStore.h"
#include "Theme.h"
#include "panels/EpgPanel.h"
#include "panels/FormHelpers.h"
#include "panels/IpPanel.h"
#include "panels/MonitorPanel.h"
#include "panels/OutputPanel.h"
#include "panels/OverviewPanel.h"
#include "panels/PidTable.h"
#include "panels/ServicesPanel.h"
#include "panels/TuningPanel.h"
#include "video/VideoWidget.h"
#include "widgets/LockChain.h"
#include "widgets/MetricTile.h"
#include "widgets/StatusPill.h"
#ifdef TBS_HAVE_FFMPEG
#include "video/AvSync.h"
#include "video/VideoDecoder.h"
#ifdef TBS_HAVE_AUDIO
#include "video/AudioOutput.h"
#endif
#endif

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

QScrollArea *scrollWrap(QWidget *w)
{
    auto *sa = new QScrollArea;
    sa->setWidget(w);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return sa;
}

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QString deviceText(const tbs::DeviceSelection &d)
{
    switch (d.kind) {
    case tbs::DeviceKind::LinuxDvb:
        return QStringLiteral("adapter%1 / frontend%2").arg(d.adapter).arg(d.frontend);
    case tbs::DeviceKind::WindowsBda:
        return QObject::tr("BDA tuner #%1").arg(d.bdaIndex);
    case tbs::DeviceKind::FileReplay:
        return QObject::tr("Replay · %1").arg(QString::fromStdString(d.replayFile).section(QLatin1Char('/'), -1));
    }
    return QString();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TBS Receiver"));
    m_controller = new ReceiverController(this);
    {
        QSettings settings;
        m_monitor = std::make_unique<tbs::Monitor>(SettingsStore::loadMonitor(settings));
    }
#ifdef TBS_HAVE_FFMPEG
    m_decoder = new VideoDecoder(this);
    VideoDecoder *dec = m_decoder;
    m_controller->setPreviewSink([dec](const uint8_t *p, size_t n) { dec->consumeTs(p, n); });
#endif

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildHeader());
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildSidebar());
    body->addWidget(buildMain(), 1);
    v->addLayout(body, 1);
    setCentralWidget(page);

    m_statusText = new QLabel;
    statusBar()->addWidget(m_statusText, 1);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("tbsdvb %1").arg(QString::fromLatin1(tbs::version()))));
    statusBar()->setSizeGripEnabled(false);

    connect(m_controller, &ReceiverController::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_controller, &ReceiverController::statusUpdated, this, &MainWindow::onStatus);
    connect(m_controller, &ReceiverController::statsUpdated, this, &MainWindow::onStats);
    connect(m_controller, &ReceiverController::logMessage, this, &MainWindow::appendLog);
    // Alarms arrive on the GUI thread (onStats); they go to the log as well
    m_monitor->setAlarmCallback([this](const tbs::AlarmEvent &e) {
        const QString title = tr(tbs::alarmTypeTitle(e.type));
        const QString msg = QString::fromStdString(e.message);
        QMetaObject::invokeMethod(this, [this, raised = e.raised, title, msg]() {
            appendLog(raised ? QStringLiteral("WARNING: alarm: %1 (%2)").arg(title, msg)
                             : tr("Alarm cleared: %1 (%2)").arg(title, msg));
            updateAlarmIndicator();
        }, Qt::QueuedConnection);
    });
    connect(m_monitorPanel, &MonitorPanel::configChanged, this, [](const tbs::MonitorConfig &cfg) {
        QSettings settings;
        SettingsStore::saveMonitor(settings, cfg);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
        if (m_tabs->currentWidget() == m_monitorPanel)
            m_monitorPanel->refresh();
    });
    connect(m_epg, &EpgPanel::watchRequested, this, [this](int isi, int program, int audio) {
        selectPreview(isi, program, audio);
    });
    connect(m_services, &ServicesPanel::watchRequested, this, [this](int isi, int program, int audio) {
        selectPreview(isi, program, audio);
        if (!m_output->videoPreview())
            appendLog(tr("Switch on the video preview under Outputs to watch the service"));
    });
#ifdef TBS_HAVE_FFMPEG
#ifdef TBS_HAVE_AUDIO
    m_audio = new AudioOutput(this);
    AudioOutput *audio = m_audio;
    m_avsync = new AvSync(m_video, [audio](double *pts) { return audio->clock(pts); }, this);
    connect(m_decoder, &VideoDecoder::audioReady, m_audio, &AudioOutput::push, Qt::QueuedConnection);
#else
    m_avsync = new AvSync(m_video, nullptr, this);
#endif
    connect(m_decoder, &VideoDecoder::frameReady, m_avsync, &AvSync::push, Qt::QueuedConnection);
    connect(m_decoder, &VideoDecoder::sessionStarted, this, [this]() {
        m_avsync->reset();
#ifdef TBS_HAVE_AUDIO
        m_audio->reset();
#endif
    }, Qt::QueuedConnection);
#ifdef TBS_HAVE_AUDIO
    connect(m_decoder, &VideoDecoder::audioRestarted, m_audio, &AudioOutput::reset, Qt::QueuedConnection);
#endif
    connect(m_decoder, &VideoDecoder::streamInfo, this, [this](const QString &info) {
        appendLog(tr("Preview: %1").arg(info));
        // Chip: codec and size of each stream, without the PIDs
        QStringList chip;
        for (const QString &part : info.split(QStringLiteral(" · ")))
            chip << part.section(QStringLiteral(", PID"), 0, 0).replace(QStringLiteral(", "), QStringLiteral(" "));
        m_video->setInfo(chip.join(QStringLiteral(" · ")));
        m_video->setMessage(info);
    }, Qt::QueuedConnection);
#endif

    m_procTimer = new QTimer(this);
    m_procTimer->setInterval(1000);
    connect(m_procTimer, &QTimer::timeout, this, &MainWindow::refreshProcessing);

    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, &MainWindow::toggleCapture);

    loadSettings();
    resetMetrics();
    setRunningUi(false);
    appendLog(tr("Ready · tbsdvb %1").arg(QString::fromLatin1(tbs::version())));
}

MainWindow::~MainWindow()
{
    m_controller->setPreviewEnabled(false);
    m_controller->stop();
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
#endif
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("Header"));
    header->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(18, 12, 18, 12);
    h->setSpacing(12);

    m_logo = new QLabel;
    h->addWidget(m_logo);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto *title = new QLabel(tr("TBS Receiver"));
    title->setObjectName(QStringLiteral("AppTitle"));
    m_subtitle = new QLabel;
    m_subtitle->setObjectName(QStringLiteral("AppSubtitle"));
    titles->addWidget(title);
    titles->addWidget(m_subtitle);
    h->addLayout(titles);
    h->addStretch();

    m_pill = new StatusPill;
    h->addWidget(m_pill);
    h->addSpacing(6);

    m_themeButton = new QToolButton;
    m_themeButton->setObjectName(QStringLiteral("Ghost"));
    m_themeButton->setToolTip(tr("Switch light / dark"));
    m_themeButton->setIconSize(QSize(18, 18));
    connect(m_themeButton, &QToolButton::clicked, this, &MainWindow::toggleTheme);
    h->addWidget(m_themeButton);

    m_startButton = new QPushButton;
    m_startButton->setObjectName(QStringLiteral("Primary"));
    m_startButton->setMinimumWidth(110);
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_startButton->setToolTip(tr("Tune and capture (F5)"));
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::toggleCapture);
    h->addWidget(m_startButton);
    return header;
}

QWidget *MainWindow::buildSidebar()
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("Sidebar"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(400);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(0, 14, 0, 0);
    v->setSpacing(6);

    auto *segBar = new QWidget;
    segBar->setObjectName(QStringLiteral("SegmentBar"));
    segBar->setAttribute(Qt::WA_StyledBackground);
    auto *seg = new QHBoxLayout(segBar);
    seg->setContentsMargins(3, 3, 3, 3);
    seg->setSpacing(3);
    m_tuningTab = new QToolButton;
    m_tuningTab->setText(tr("Tuning"));
    m_outputTab = new QToolButton;
    m_outputTab->setText(tr("Outputs"));
    auto *group = new QButtonGroup(this);
    for (QToolButton *b : {m_tuningTab, m_outputTab}) {
        b->setObjectName(QStringLiteral("Segment"));
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b);
        seg->addWidget(b);
    }
    m_tuningTab->setChecked(true);
    auto *segWrap = new QHBoxLayout;
    segWrap->setContentsMargins(18, 0, 18, 0);
    segWrap->addWidget(segBar);
    v->addLayout(segWrap);

    m_tuning = new TuningPanel;
    m_output = new OutputPanel;
    m_stack = new QStackedWidget;
    m_stack->addWidget(scrollWrap(m_tuning));
    m_stack->addWidget(scrollWrap(m_output));
    v->addWidget(m_stack, 1);
    connect(m_tuningTab, &QToolButton::clicked, this, [this]() { m_stack->setCurrentIndex(0); });
    connect(m_outputTab, &QToolButton::clicked, this, [this]() { m_stack->setCurrentIndex(1); });

    m_applyButton = new QPushButton(tr("Apply tuning"));
    m_applyButton->setToolTip(tr("Retune the running receiver without restarting the capture"));
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(18, 8, 18, 14);
    footer->addWidget(m_applyButton);
    v->addLayout(footer);
    connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::applyTuning);

    connect(m_tuning, &TuningPanel::tuningEdited, this, [this]() {
        m_tuningDirty = true;
        m_applyButton->setEnabled(m_running);
    });
    connect(m_tuning, &TuningPanel::captureModeChanged, this, [this]() {
        tbs::ReceiverConfig cfg;
        m_tuning->applyTo(cfg);
        m_output->setCaptureMode(cfg.capture);
        m_overview->setBbFrameMode(cfg.capture == tbs::CaptureMode::BBFrame);
    });
    connect(m_output, &OutputPanel::videoPreviewToggled, this, &MainWindow::setPreview);
    connect(m_tuning, &TuningPanel::diagnoseRequested, this, &MainWindow::runDiagnostics);
    return side;
}

QWidget *MainWindow::buildMain()
{
    auto *main = new QWidget;
    auto *v = new QVBoxLayout(main);
    v->setContentsMargins(18, 16, 18, 12);
    v->setSpacing(12);

    // Video + acquisition chain
    auto *top = new QWidget;
    auto *tv = new QVBoxLayout(top);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(12);
    // Preview selection: service, audio track, volume
    auto *pv = new QHBoxLayout;
    pv->setSpacing(8);
    m_previewService = new QComboBox;
    m_previewService->setToolTip(tr("Service shown in the preview (double-click a service in the Services tab too)"));
    m_previewAudio = new QComboBox;
    m_previewAudio->setToolTip(tr("Audio track of the preview"));
    for (QComboBox *c : {m_previewService, m_previewAudio}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(12);
    }
    m_muteButton = new QToolButton;
    m_muteButton->setCheckable(true);
    m_muteButton->setToolTip(tr("Mute"));
    m_muteButton->setAutoRaise(true);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(110);
    m_volume->setToolTip(tr("Volume"));
    pv->addWidget(keyLabel(tr("Service")));
    pv->addWidget(m_previewService, 2);
    pv->addWidget(keyLabel(tr("Audio")));
    pv->addWidget(m_previewAudio, 1);
    pv->addWidget(m_muteButton);
    pv->addWidget(m_volume);
    tv->addLayout(pv);
    m_previewService->addItem(tr("Automatic (first service with video)"), QVariantList{-1, 0});
    m_previewAudio->addItem(tr("Default track"), -1);
    m_previewAudio->addItem(tr("Off"), 0);
#ifndef TBS_HAVE_AUDIO
    m_previewAudio->setEnabled(false);
    m_previewAudio->setToolTip(tr("This build has no audio support (needs Qt Multimedia)"));
    m_muteButton->setVisible(false);
    m_volume->setVisible(false);
#endif
    connect(m_previewService, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        const QVariantList v = m_previewService->itemData(i).toList();
        selectPreview(v.value(0).toInt(), v.value(1).toInt(), -1);
    });
    connect(m_previewAudio, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        selectPreview(m_previewIsi, m_previewProgram, m_previewAudio->itemData(i).toInt());
    });
#ifdef TBS_HAVE_AUDIO
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (m_audio)
            m_audio->setVolume(v / 100.0);
    });
    connect(m_muteButton, &QToolButton::toggled, this, [this](bool m) {
        if (m_audio)
            m_audio->setMuted(m);
        refreshIcons();
    });
#endif

    m_video = new VideoWidget;
    tv->addWidget(m_video, 1);

    auto *chainCard = new QFrame;
    chainCard->setObjectName(QStringLiteral("Card"));
    auto *cl = new QHBoxLayout(chainCard);
    cl->setContentsMargins(16, 10, 16, 6);
    auto *chainTitle = Theme::sectionTitle(tr("Acquisition"));
    chainTitle->setStyleSheet(QStringLiteral("padding-top: 0px;"));
    cl->addWidget(chainTitle, 0, Qt::AlignTop);
    m_chain = new LockChain;
    cl->addWidget(m_chain, 1);
    tv->addWidget(chainCard);

    // KPI row
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(12);
    m_levelTile = new MetricTile(tr("Signal level"));
    m_cnrTile = new MetricTile(tr("C/N"));
    m_rateTile = new MetricTile(tr("Bitrate"));
    m_errorTile = new MetricTile(tr("Errors"));
    m_cnrTile->setValueFormat(1, QStringLiteral("dB"));
    m_rateTile->setValueFormat(2, QStringLiteral("Mbit/s"));
    m_errorTile->setValueFormat(0, tr("CC errors"));
    for (MetricTile *t : {m_levelTile, m_cnrTile, m_rateTile, m_errorTile})
        tiles->addWidget(t);
    tv->addLayout(tiles);

    // Details
    auto *tabs = new QTabWidget;
    m_tabs = tabs;
    tabs->setDocumentMode(true);
    m_overview = new OverviewPanel;
    m_pids = new PidTable;
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(12);
    m_log->setFont(mono);

    auto wrapCard = [](QWidget *w) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("Card"));
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(6, 6, 6, 6);
        l->addWidget(w);
        auto *outer = new QWidget;
        auto *ol = new QVBoxLayout(outer);
        ol->setContentsMargins(0, 12, 0, 0);
        ol->addWidget(card);
        return outer;
    };
    m_services = new ServicesPanel;
    m_ip = new IpPanel;
    tabs->addTab(scrollWrap(m_overview), tr("Overview"));
    tabs->addTab(m_services, tr("Services"));
    m_epg = new EpgPanel;
    tabs->addTab(m_epg, tr("EPG"));
    m_monitorPanel = new MonitorPanel(m_monitor.get());
    tabs->addTab(m_monitorPanel, tr("Monitor"));
    tabs->addTab(scrollWrap(m_ip), tr("IP / GSE"));
    tabs->addTab(wrapCard(m_pids), tr("PIDs"));
    m_logTab = wrapCard(m_log);
    tabs->addTab(m_logTab, tr("Log"));

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(top);
    split->addWidget(tabs);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);
    v->addWidget(split, 1);
    return main;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void MainWindow::loadSettings()
{
    QSettings s;
    m_config = SettingsStore::load(s);
    const bool preview = s.value(QStringLiteral("ui/videoPreview"), true).toBool();
    m_tuning->setConfig(m_config);
    m_output->setConfig(m_config, preview);
    m_output->setProcessing(SettingsStore::loadProcessing(s));
    m_overview->setBbFrameMode(m_config.capture == tbs::CaptureMode::BBFrame);
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    m_tuningDirty = false;
    m_previewIsi = s.value(QStringLiteral("preview/isi"), -1).toInt();
    m_previewProgram = s.value(QStringLiteral("preview/program"), 0).toInt();
    m_previewAudioPid = s.value(QStringLiteral("preview/audioPid"), -1).toInt();
    m_volume->setValue(s.value(QStringLiteral("preview/volume"), 80).toInt());
    m_muteButton->setChecked(s.value(QStringLiteral("preview/muted"), false).toBool());
    applyPreviewSelection();
}

void MainWindow::saveSettings()
{
    QSettings s;
    SettingsStore::save(s, currentConfig());
    SettingsStore::saveProcessing(s, m_output->processing());
    s.setValue(QStringLiteral("ui/videoPreview"), m_output->videoPreview());
    s.setValue(QStringLiteral("ui/theme"), Theme::mode() == Theme::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("preview/isi"), m_previewIsi);
    s.setValue(QStringLiteral("preview/program"), m_previewProgram);
    s.setValue(QStringLiteral("preview/audioPid"), m_previewAudioPid);
    s.setValue(QStringLiteral("preview/volume"), m_volume->value());
    s.setValue(QStringLiteral("preview/muted"), m_muteButton->isChecked());
}

tbs::ReceiverConfig MainWindow::currentConfig() const
{
    tbs::ReceiverConfig cfg = m_config;
    m_tuning->applyTo(cfg);
    m_output->applyTo(cfg);
    return cfg;
}

// ---------------------------------------------------------------------------
// Capture control
// ---------------------------------------------------------------------------

void MainWindow::toggleCapture()
{
    if (m_running)
        stopCapture();
    else
        startCapture();
}

void MainWindow::startCapture()
{
    if (m_controller->isRunning())
        return;
    m_config = currentConfig();
    if (m_config.device.kind == tbs::DeviceKind::FileReplay && m_config.device.replayFile.empty()) {
        QMessageBox::warning(this, windowTitle(), tr("Select a file to replay."));
        return;
    }
    saveSettings();
    m_hadError = false;
    m_tuningDirty = false;
    resetMetrics();
    m_overview->setBbFrameMode(m_config.capture == tbs::CaptureMode::BBFrame);
    setPreview(m_output->videoPreview());
    setRunningUi(true);
    m_pill->setState(StatusPill::Kind::Accent, tr("STARTING"));
    m_subtitle->setText(deviceText(m_config.device));
    m_statusText->setText(QString::fromStdString(m_config.tuning.describe()));

    // Content processing: TS tables, GSE decapsulation, IP statistics, pcap
    const tbs::bb::ProcessorConfig pc = m_output->processing();
    m_processor = std::make_shared<tbs::bb::Processor>(pc);
    std::string perr;
    if (!m_processor->open(&perr))
        appendLog(tr("ERROR: IP output: %1").arg(QString::fromStdString(perr)));
    {
        const tbs::bb::ProcessorSnapshot first = m_processor->snapshot();
        if (pc.pcap.enabled && first.pcapOpen)
            appendLog(tr("Writing IP packets (GSE/MPE) to %1").arg(QString::fromStdString(first.pcapFile)));
        if (first.tunOpen)
            appendLog(tr("Sending IP packets to interface %1").arg(QString::fromStdString(first.tunName)));
        if (!first.tunWarning.empty())
            appendLog(tr("WARNING: %1").arg(QString::fromStdString(first.tunWarning)));
    }
    m_controller->setProcessor(m_processor);
    m_services->reset();
    m_monitor->reset();
    m_lastStatus = tbs::FrontendStatus();
    m_epg->reset();
    m_epgState.clear();
    m_ip->reset();
    m_procTimer->start();

    m_controller->start(m_config);
}

void MainWindow::stopCapture()
{
    m_controller->setPreviewEnabled(false);
    m_controller->stop();
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
#endif
#ifdef TBS_HAVE_AUDIO
    m_audio->stop();
    m_audioStarted = false;
#endif
}

void MainWindow::applyTuning()
{
    if (!m_running)
        return;
    const tbs::TuningParams t = m_tuning->tuning();
    m_controller->retune(t);
    m_config.tuning = t;
    m_tuningDirty = false;
    m_applyButton->setEnabled(false);
    m_statusText->setText(QString::fromStdString(t.describe()));
    resetMetrics();
    appendLog(tr("Retuning: %1").arg(QString::fromStdString(t.describe())));
}

void MainWindow::setPreview(bool on)
{
#ifdef TBS_HAVE_FFMPEG
#ifdef TBS_HAVE_AUDIO
    if (on && m_audio->available()) {
        if (!m_audioStarted) {
            m_audio->setVolume(m_volume->value() / 100.0);
            m_audio->setMuted(m_muteButton->isChecked());
            m_audio->start();
            m_audioStarted = true;
            appendLog(tr("Audio output: %1").arg(m_audio->deviceName()));
        }
    } else {
        if (on && !m_audioStarted)
            appendLog(tr("No audio output device: the preview has no sound"));
        m_audio->stop();
        m_audioStarted = false;
    }
    m_decoder->setAudioWanted(m_audio->available());
#endif
    if (on && m_running) {
        m_decoder->start();
        m_video->setMessage(tr("Waiting for video…"));
        m_controller->setPreviewEnabled(true);
    } else if (!on) {
        m_controller->setPreviewEnabled(false);
        m_decoder->stop();
        m_video->clear();
        m_video->setMessage(tr("Video preview is off"));
    } else {
        m_controller->setPreviewEnabled(true);
        m_decoder->start();
    }
#else
    Q_UNUSED(on);
    m_video->setMessage(tr("Video preview is not available in this build"));
#endif
}

void MainWindow::setRunningUi(bool running)
{
    m_running = running;
    m_tuning->setRunning(running);
    m_output->setEditable(!running);
    m_applyButton->setVisible(running);
    m_applyButton->setEnabled(running && m_tuningDirty);
    m_startButton->setObjectName(running ? QStringLiteral("Danger") : QStringLiteral("Primary"));
    m_startButton->setText(running ? tr("Stop") : tr("Start"));
    repolish(m_startButton);
    refreshIcons();
    if (!running) {
        m_pill->setState(m_hadError ? StatusPill::Kind::Critical : StatusPill::Kind::Neutral,
                         m_hadError ? tr("ERROR") : tr("IDLE"));
        m_subtitle->setText(tr("Not capturing"));
        m_chain->setFlags(0);
    }
}

void MainWindow::resetMetrics()
{
    m_levelTile->clearHistory();
    m_cnrTile->clearHistory();
    m_rateTile->clearHistory();
    m_errorTile->clearHistory();
    m_levelTile->setValue(QStringLiteral("–"));
    m_cnrTile->setValue(QStringLiteral("–"));
    m_rateTile->setValue(QStringLiteral("–"));
    m_errorTile->setValue(QStringLiteral("–"));
    m_cnrTile->setStatus(MetricTile::Status::None, QString());
    m_errorTile->setStatus(MetricTile::Status::None, QString());
    m_levelTile->setSubtitle(QString());
    m_cnrTile->setSubtitle(QString());
    m_rateTile->setSubtitle(QString());
    m_errorTile->setSubtitle(QString());
    m_overview->reset();
    m_pids->reset();
    m_chain->setFlags(0);
    m_lastCc = 0;
}

// ---------------------------------------------------------------------------
// Updates from the receiver
// ---------------------------------------------------------------------------

void MainWindow::onStateChanged(int state, const QString &error)
{
    const auto s = static_cast<tbs::ReceiverState>(state);
    if (!error.isEmpty()) {
        m_hadError = true;
        m_statusText->setText(error);
        m_tabs->setCurrentWidget(m_logTab); // the log holds the diagnostics
    }
    if (s == tbs::ReceiverState::Running) {
        m_pill->setState(StatusPill::Kind::Warning, tr("SEARCHING"));
    } else if (s == tbs::ReceiverState::Stopped) {
        m_procTimer->stop();
        if (m_processor) {
            m_processor->close();
            refreshProcessing();
            const auto snap = m_processor->snapshot();
            if (!snap.pcapFile.empty())
                appendLog(tr("pcap closed: %1 (%2 packets)").arg(QString::fromStdString(snap.pcapFile)).arg(snap.pcapPackets));
        }
        m_controller->setPreviewEnabled(false);
#ifdef TBS_HAVE_FFMPEG
        m_decoder->stop();
#endif
#ifdef TBS_HAVE_AUDIO
        m_audio->stop();
        m_audioStarted = false;
#endif
        setRunningUi(false);
    }
}

void MainWindow::onStatus(const tbs::FrontendStatus &st)
{
    if (!m_running)
        return;
    m_lastStatus = st;
    m_chain->setFlags(st.flags);
    m_pill->setState(st.locked() ? StatusPill::Kind::Good : StatusPill::Kind::Warning,
                     st.locked() ? tr("LOCKED") : tr("SEARCHING"));
    if (!st.deviceName.empty())
        m_subtitle->setText(QStringLiteral("%1 · %2").arg(QString::fromStdString(st.deviceName), deviceText(m_config.device)));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (st.hasSignalDbm) {
        m_levelTile->setValue(QString::number(st.signalDbm, 'f', 1), QStringLiteral("dBm"));
        m_levelTile->setValueFormat(1, QStringLiteral("dBm"));
        m_levelTile->addSample(st.signalDbm);
    } else if (st.hasSignalPercent) {
        m_levelTile->setValue(QString::number(st.signalPercent, 'f', 0), QStringLiteral("%"));
        m_levelTile->setValueFormat(0, QStringLiteral("%"));
        m_levelTile->addSample(st.signalPercent);
    } else {
        m_levelTile->setValue(QStringLiteral("–"));
        m_levelTile->addSample(nan);
    }

    if (st.hasCnrDb) {
        m_cnrTile->setValue(QString::number(st.cnrDb, 'f', 1), QStringLiteral("dB"));
        m_cnrTile->setValueFormat(1, QStringLiteral("dB"));
        m_cnrTile->addSample(st.cnrDb);
        if (!st.locked())
            m_cnrTile->setStatus(MetricTile::Status::Critical, tr("No lock"));
        else if (st.cnrDb >= 8.0)
            m_cnrTile->setStatus(MetricTile::Status::Good, tr("Good"));
        else if (st.cnrDb >= 4.0)
            m_cnrTile->setStatus(MetricTile::Status::Warning, tr("Marginal"));
        else
            m_cnrTile->setStatus(MetricTile::Status::Critical, tr("Poor"));
    } else if (st.hasCnrPercent) {
        m_cnrTile->setValue(QString::number(st.cnrPercent, 'f', 0), QStringLiteral("%"));
        m_cnrTile->setValueFormat(0, QStringLiteral("%"));
        m_cnrTile->addSample(st.cnrPercent);
        m_cnrTile->setStatus(MetricTile::Status::None, QString());
    } else {
        m_cnrTile->setValue(QStringLiteral("–"));
        m_cnrTile->addSample(nan);
        m_cnrTile->setStatus(st.locked() ? MetricTile::Status::Good : MetricTile::Status::None,
                             st.locked() ? tr("Locked") : QString());
    }
    m_errorTile->setSubtitle(st.hasBer ? tr("BER %1 · error blocks %2")
                                             .arg(QString::number(st.ber, 'e', 1))
                                             .arg(st.hasErrorBlocks ? QString::number(st.errorBlocks) : QStringLiteral("–"))
                                       : QString());
    m_overview->updateFrontend(st);
}

void MainWindow::onStats(const tbs::StreamStats &st)
{
    if (!m_running)
        return;
    const bool bb = m_config.capture == tbs::CaptureMode::BBFrame;
    m_rateTile->setValue(QString::number(st.inputMbps, 'f', 2), QStringLiteral("Mbit/s"));
    m_rateTile->addSample(st.inputMbps);
    m_rateTile->setSubtitle(bb ? tr("%1 BBFrames/s · payload %2 Mbit/s").arg(st.bbFramesPerSec, 0, 'f', 0).arg(st.bbPayloadMbps, 0, 'f', 2)
                               : tr("%1 TS packets").arg(QLocale().toString(static_cast<qulonglong>(st.tsPackets))));

    const uint64_t errors = st.ccErrors + st.teiErrors + st.bbCrcErrors + st.bbSequenceErrors + st.driverOverflows;
    const uint64_t newCc = st.ccErrors >= m_lastCc ? st.ccErrors - m_lastCc : 0;
    m_lastCc = st.ccErrors;
    m_errorTile->setValue(QLocale().toString(static_cast<qulonglong>(errors)), tr("total"));
    m_errorTile->addSample(static_cast<double>(newCc));
    m_errorTile->setStatus(newCc ? MetricTile::Status::Critical : MetricTile::Status::Good, newCc ? tr("Errors") : tr("Clean"));

    m_overview->updateStats(st);
    m_pids->updateStats(st);
    m_output->setNetworkStatus(st.network);

    m_monitor->update(tbs::ReceiverState::Running, m_lastStatus, st);
    if (m_tabs->currentWidget() == m_monitorPanel)
        m_monitorPanel->refresh();
    updateAlarmIndicator();
}

void MainWindow::updateAlarmIndicator()
{
    const size_t n = m_monitor->activeAlarms();
    const int idx = m_tabs->indexOf(m_monitorPanel);
    m_tabs->setTabText(idx, n ? tr("Monitor (%1)").arg(n) : tr("Monitor"));
}

void MainWindow::appendLog(const QString &message)
{
    const Theme::Tokens &t = Theme::tokens();
    QString marker;
    if (message.startsWith(QLatin1String("ERROR")))
        marker = QStringLiteral("<span style='color:%1'>● </span>").arg(t.critical.name());
    else if (message.startsWith(QLatin1String("WARNING")))
        marker = QStringLiteral("<span style='color:%1'>● </span>").arg(t.warning.name());
    m_log->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;%3<span style='color:%4'>%5</span>")
                          .arg(t.muted.name(), QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), marker,
                               t.ink.name(), message.toHtmlEscaped()));
}

void MainWindow::showSidebar(const QString &name)
{
    const bool outputs = name.compare(QLatin1String("outputs"), Qt::CaseInsensitive) == 0;
    (outputs ? m_outputTab : m_tuningTab)->setChecked(true);
    m_stack->setCurrentIndex(outputs ? 1 : 0);
}

void MainWindow::showTab(const QString &name)
{
    static const QStringList names = {QStringLiteral("overview"), QStringLiteral("services"), QStringLiteral("epg"),
                                      QStringLiteral("monitor"), QStringLiteral("ip"), QStringLiteral("pids"),
                                      QStringLiteral("log")};
    const int i = names.indexOf(name.toLower());
    if (i >= 0)
        m_tabs->setCurrentIndex(i);
}

void MainWindow::refreshProcessing()
{
    if (!m_processor)
        return;
    const tbs::bb::ProcessorSnapshot snap = m_processor->snapshot();
    m_services->updateSnapshot(snap);
    m_ip->updateSnapshot(snap);
    m_overview->updateContent(snap);
    // In TS mode the PID table shows what each PID carries
    for (const auto &st : snap.streams)
        if (st.content == tbs::bb::StreamContent::TsInput)
            m_pids->setPidUsage(st.ts.pidUsage);

    // EPG: fetched when it changed (at most every 5 s unless the tab is open) and each
    // half minute so the programme running now moves on
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool epgVisible = m_tabs->currentWidget() == m_epg;
    for (const auto &st : snap.streams) {
        if (st.ts.epgEvents == 0)
            continue;
        EpgState &e = m_epgState[st.isi];
        const bool changed = st.ts.epgVersion != e.version;
        const qint64 minGap = epgVisible ? 1000 : 5000;
        if ((changed && nowMs - e.fetchedMs >= minGap) || nowMs - e.fetchedMs >= 30000) {
            e.version = st.ts.epgVersion;
            e.fetchedMs = nowMs;
            m_epg->setSchedules(st.isi, m_processor->epg(st.isi), st.ts.hasTime ? st.ts.utcTime : 0);
        }
    }

    // Services offered for network outputs
    QVector<ServiceChoice> services;
    for (const auto &st : snap.streams) {
        for (const auto &p : st.ts.programs) {
            if (p.programNumber == 0)
                continue;
            ServiceChoice c;
            c.programNumber = p.programNumber;
            c.isi = st.isi;
            for (const auto &sv : st.ts.services)
                if (sv.actual && sv.serviceId == p.programNumber)
                    c.name = QString::fromStdString(sv.name);
            services.push_back(c);
        }
    }
    if (!services.isEmpty())
        m_output->setServices(services);
    updatePreviewChoices(snap);
}

void MainWindow::selectPreview(int isi, int programNumber, int audioPid)
{
    m_previewIsi = isi;
    m_previewProgram = programNumber;
    m_previewAudioPid = audioPid;
    applyPreviewSelection();
    m_previewChoicesSig = 0; // rebuild the combos (audio tracks of the new service)
    if (m_processor)
        updatePreviewChoices(m_processor->snapshot());
}

void MainWindow::applyPreviewSelection()
{
    m_controller->setPreviewStream(m_previewIsi);
#ifdef TBS_HAVE_FFMPEG
    // The decoder signals sessionStarted / audioRestarted, which drop the queued media
    m_decoder->setSelection(m_previewProgram, m_previewAudioPid);
#endif
}

void MainWindow::updatePreviewChoices(const tbs::bb::ProcessorSnapshot &snap)
{
    struct Track
    {
        int pid;
        QString label;
    };
    struct Svc
    {
        int isi;
        int program;
        QString label;
        std::vector<Track> audio;
    };
    const bool bb = m_config.capture == tbs::CaptureMode::BBFrame;
    std::vector<Svc> list;
    for (const auto &st : snap.streams) {
        for (const auto &p : st.ts.programs) {
            if (p.programNumber == 0)
                continue;
            Svc sv{st.isi, p.programNumber, QString(), {}};
            QString name;
            for (const auto &x : st.ts.services)
                if (x.actual && x.serviceId == p.programNumber)
                    name = QString::fromStdString(x.name);
            bool video = false;
            for (const auto &es : p.streams) {
                video = video || es.kind == "Video";
                if (es.kind == "Audio")
                    sv.audio.push_back({es.pid, QStringLiteral("%1 · %2 · 0x%3")
                                                    .arg(es.language.empty() ? tr("und") : QString::fromStdString(es.language))
                                                    .arg(QString::fromStdString(es.codec))
                                                    .arg(es.pid, 4, 16, QLatin1Char('0'))});
            }
            sv.label = QStringLiteral("%1 · %2").arg(p.programNumber).arg(name.isEmpty() ? tr("Program %1").arg(p.programNumber) : name);
            if (bb && st.isi >= 0)
                sv.label += tr(" · ISI %1").arg(st.isi);
            if (p.pmtSeen && !video && !sv.audio.empty())
                sv.label += tr(" (radio)");
            list.push_back(sv);
        }
    }
    // Rebuild only when something changed
    std::string sig = std::to_string(m_previewIsi) + '/' + std::to_string(m_previewProgram) + '/'
                      + std::to_string(m_previewAudioPid);
    for (const Svc &sv : list) {
        sig += '|' + sv.label.toStdString();
        for (const Track &t : sv.audio)
            sig += ',' + t.label.toStdString();
    }
    const size_t h = std::hash<std::string>()(sig);
    if (h == m_previewChoicesSig)
        return;
    m_previewChoicesSig = h;

    QSignalBlocker b1(m_previewService);
    QSignalBlocker b2(m_previewAudio);
    m_previewService->clear();
    m_previewService->addItem(tr("Automatic (first service with video)"), QVariantList{-1, 0});
    const Svc *current = nullptr;
    for (const Svc &sv : list) {
        m_previewService->addItem(sv.label, QVariantList{sv.isi, sv.program});
        if (sv.program == m_previewProgram && (m_previewIsi < 0 || sv.isi < 0 || sv.isi == m_previewIsi)) {
            m_previewService->setCurrentIndex(m_previewService->count() - 1);
            current = &sv;
        }
    }
    if (m_previewProgram > 0 && !current) {
        m_previewService->addItem(tr("%1 · not in this stream").arg(m_previewProgram), QVariantList{m_previewIsi, m_previewProgram});
        m_previewService->setCurrentIndex(m_previewService->count() - 1);
    }
    m_previewAudio->clear();
    m_previewAudio->addItem(tr("Default track"), -1);
    if (current)
        for (const Track &t : current->audio)
            m_previewAudio->addItem(t.label, t.pid);
    m_previewAudio->addItem(tr("Off"), 0);
    int ai = m_previewAudio->findData(m_previewAudioPid);
    if (ai < 0 && m_previewAudioPid > 0) {
        m_previewAudio->addItem(tr("PID 0x%1").arg(m_previewAudioPid, 4, 16, QLatin1Char('0')), m_previewAudioPid);
        ai = m_previewAudio->count() - 1;
    }
    m_previewAudio->setCurrentIndex(std::max(0, ai));
}

void MainWindow::runDiagnostics()
{
    const tbs::ReceiverConfig cfg = currentConfig();
    appendLog(tr("Diagnostics for %1:").arg(deviceText(cfg.device)));
    const auto items = tbs::Devices::diagnose(cfg.device);
    bool ok = true;
    for (const auto &i : items) {
        const char *tag = "";
        switch (i.level) {
        case tbs::DiagnosticItem::Level::Ok:
            tag = "OK      ";
            break;
        case tbs::DiagnosticItem::Level::Info:
            tag = "INFO    ";
            break;
        case tbs::DiagnosticItem::Level::Warning:
            tag = "WARNING ";
            break;
        case tbs::DiagnosticItem::Level::Error:
            tag = "ERROR   ";
            ok = false;
            break;
        }
        appendLog(QString::fromLatin1(tag) + QString::fromStdString(i.message));
        if (!i.hint.empty())
            appendLog(QStringLiteral("        → ") + QString::fromStdString(i.hint));
    }
    appendLog(ok ? tr("Diagnostics: no problems found") : tr("Diagnostics: problems found, see the hints above"));
    m_tabs->setCurrentWidget(m_logTab);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void MainWindow::toggleTheme()
{
    Theme::apply(*qApp, Theme::mode() == Theme::Mode::Dark ? Theme::Mode::Light : Theme::Mode::Dark);
    refreshIcons();
    for (QWidget *w : findChildren<QWidget *>())
        w->update();
}

void MainWindow::refreshIcons()
{
    const Theme::Tokens &t = Theme::tokens();
    m_logo->setPixmap(Theme::icon(QStringLiteral("logo"), t.accent, 34).pixmap(34, 34));
    m_muteButton->setIcon(Theme::icon(m_muteButton->isChecked() ? QStringLiteral("mute") : QStringLiteral("volume"), t.ink2));
    m_themeButton->setIcon(Theme::icon(Theme::mode() == Theme::Mode::Dark ? QStringLiteral("sun") : QStringLiteral("moon"), t.ink2));
    m_startButton->setIcon(Theme::icon(m_running ? QStringLiteral("stop") : QStringLiteral("play"), QColor(Qt::white), 14));
    m_applyButton->setIcon(Theme::icon(QStringLiteral("sync"), t.ink2, 16));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    stopCapture();
    event->accept();
}
