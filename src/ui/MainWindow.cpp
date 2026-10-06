#include "MainWindow.h"

#include "OutputPanel.h"
#include "StatusPanel.h"
#include "TuningPanel.h"
#include "core/BbFrameExtractor.h"
#include "core/ReceiverEngine.h"
#include "video/VideoWidget.h"
#ifdef TBS_HAVE_FFMPEG
#include "video/VideoDecoder.h"
#endif

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
QScrollArea *wrapScroll(QWidget *w)
{
    auto *sa = new QScrollArea;
    sa->setWidget(w);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return sa;
}

QString pidDescription(uint16_t pid)
{
    if (BbFrameExtractor::isBbFramePid(pid))
        return QObject::tr("BBFrame carrier");
    switch (pid) {
    case 0x0000:
        return QStringLiteral("PAT");
    case 0x0001:
        return QStringLiteral("CAT");
    case 0x0010:
        return QStringLiteral("NIT");
    case 0x0011:
        return QStringLiteral("SDT/BAT");
    case 0x0012:
        return QStringLiteral("EIT");
    case 0x0014:
        return QStringLiteral("TDT/TOT");
    case 0x1FFF:
        return QObject::tr("Null packets");
    default:
        return QString();
    }
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_engine = new ReceiverEngine(this);
#ifdef TBS_HAVE_FFMPEG
    m_decoder = new VideoDecoder(this);
#endif
    buildUi();
    loadSettings();

    connect(m_engine, &ReceiverEngine::started, this, &MainWindow::onStarted);
    connect(m_engine, &ReceiverEngine::stopped, this, &MainWindow::onStopped);
    connect(m_engine, &ReceiverEngine::failed, this, &MainWindow::onFailed);
    connect(m_engine, &ReceiverEngine::logMessage, this, &MainWindow::appendLog);
    connect(m_engine, &ReceiverEngine::frontendStatusChanged, m_status, &StatusPanel::updateFrontend);
    connect(m_engine, &ReceiverEngine::streamStatsChanged, m_status, &StatusPanel::updateStats);
    connect(m_engine, &ReceiverEngine::streamStatsChanged, this, &MainWindow::updatePidTable);
#ifdef TBS_HAVE_FFMPEG
    connect(m_decoder, &VideoDecoder::frameReady, m_video, &VideoWidget::setFrame, Qt::QueuedConnection);
    connect(m_decoder, &VideoDecoder::streamInfo, this, [this](const QString &info) {
        appendLog(tr("Video: %1").arg(info));
        m_video->setMessage(info);
    }, Qt::QueuedConnection);
#endif

    appendLog(tr("TBS Receiver %1 ready").arg(QStringLiteral(TBS_APP_VERSION)));
}

MainWindow::~MainWindow()
{
    m_engine->setPreviewConsumer(nullptr);
    m_engine->stop();
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
#endif
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("TBS 6903x Receiver"));

    // ---- Toolbar ----
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    tb->setMovable(false);
    m_startAction = tb->addAction(tr("Start"), this, &MainWindow::startCapture);
    m_startAction->setShortcut(Qt::Key_F5);
    m_startAction->setToolTip(tr("Tune and start capture (F5)"));
    m_stopAction = tb->addAction(tr("Stop"), this, &MainWindow::stopCapture);
    m_stopAction->setShortcut(Qt::SHIFT | Qt::Key_F5);
    m_stopAction->setEnabled(false);
    tb->addSeparator();
    QAction *rescan = tb->addAction(tr("Rescan devices"));
    tb->addSeparator();
    QAction *clearLog = tb->addAction(tr("Clear log"));
    QAction *about = tb->addAction(tr("About"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    // ---- Left: settings ----
    m_tuning = new TuningPanel;
    m_output = new OutputPanel;
    auto *settingsTabs = new QTabWidget;
    settingsTabs->addTab(wrapScroll(m_tuning), tr("Tuning"));
    settingsTabs->addTab(wrapScroll(m_output), tr("Output"));
    settingsTabs->setMinimumWidth(380);
    connect(rescan, &QAction::triggered, m_tuning, &TuningPanel::refreshDevices);
    connect(m_tuning, &TuningPanel::captureModeChanged, this, [this]() {
        TunerConfig cfg;
        m_tuning->applyTo(cfg);
        m_output->setCaptureMode(cfg.captureMode);
        m_status->setBbFrameMode(cfg.captureMode == CaptureMode::BBFrame);
    });

    // ---- Right: video / PIDs + status ----
    m_video = new VideoWidget;
#ifndef TBS_HAVE_FFMPEG
    m_video->setMessage(tr("Video preview is not available (built without FFmpeg)"));
#endif
    m_pidTable = new QTableWidget(0, 4);
    m_pidTable->setHorizontalHeaderLabels({tr("PID"), tr("Packets"), tr("Share"), tr("Description")});
    m_pidTable->horizontalHeader()->setStretchLastSection(true);
    m_pidTable->verticalHeader()->setVisible(false);
    m_pidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pidTable->setSelectionMode(QAbstractItemView::NoSelection);

    auto *viewTabs = new QTabWidget;
    viewTabs->addTab(m_video, tr("Video"));
    viewTabs->addTab(m_pidTable, tr("PIDs"));

    m_status = new StatusPanel;

    auto *rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->addWidget(viewTabs);
    rightSplit->addWidget(wrapScroll(m_status));
    rightSplit->setStretchFactor(0, 3);
    rightSplit->setStretchFactor(1, 2);

    auto *topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(settingsTabs);
    topSplit->addWidget(rightSplit);
    topSplit->setStretchFactor(0, 0);
    topSplit->setStretchFactor(1, 1);

    // ---- Bottom: log ----
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFont(QStringLiteral("monospace")));
    connect(clearLog, &QAction::triggered, m_log, &QPlainTextEdit::clear);

    auto *mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(m_log);
    mainSplit->setStretchFactor(0, 5);
    mainSplit->setStretchFactor(1, 1);
    setCentralWidget(mainSplit);

    // ---- Status bar ----
    m_stateLabel = new QLabel(tr("Idle"));
    m_rateLabel = new QLabel;
    statusBar()->addWidget(m_stateLabel, 1);
    statusBar()->addPermanentWidget(m_rateLabel);
    connect(m_engine, &ReceiverEngine::streamStatsChanged, this, [this](const StreamStats &st) {
        m_rateLabel->setText(tr("%1 Mbit/s").arg(st.inputMbps, 0, 'f', 2));
    });
    connect(m_engine, &ReceiverEngine::frontendStatusChanged, this, [this](const FrontendStatus &st) {
        if (m_engine->isRunning())
            m_stateLabel->setText(st.locked() ? tr("Running - locked") : tr("Running - searching / no lock"));
    });

    resize(1400, 900);
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_config.load(s);
    m_tuning->setConfig(m_config);
    m_output->setConfig(m_config);
    m_status->setBbFrameMode(m_config.captureMode == CaptureMode::BBFrame);
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    currentConfig().save(s);
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
}

TunerConfig MainWindow::currentConfig() const
{
    TunerConfig cfg = m_config;
    m_tuning->applyTo(cfg);
    m_output->applyTo(cfg);
    return cfg;
}

void MainWindow::setRunningUi(bool running)
{
    m_startAction->setEnabled(!running);
    m_stopAction->setEnabled(running);
    m_tuning->setEditable(!running);
    m_output->setEditable(!running);
}

void MainWindow::startCapture()
{
    if (m_engine->isRunning())
        return;
    m_config = currentConfig();
    if (m_config.deviceKind == DeviceKind::FileReplay && m_config.replayFile.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Select a file to replay."));
        return;
    }
    saveSettings();

    m_status->reset();
    m_status->setBbFrameMode(m_config.captureMode == CaptureMode::BBFrame);
    m_pidTable->setRowCount(0);
    m_video->clear();

#ifdef TBS_HAVE_FFMPEG
    if (m_config.videoPreview) {
        m_video->setMessage(tr("Waiting for video..."));
        m_decoder->start();
        m_engine->setPreviewConsumer(m_decoder);
    } else {
        m_video->setMessage(tr("Video preview disabled"));
        m_engine->setPreviewConsumer(nullptr);
    }
#endif

    setRunningUi(true);
    m_stateLabel->setText(tr("Starting..."));
    m_engine->start(m_config);
}

void MainWindow::stopCapture()
{
    m_stateLabel->setText(tr("Stopping..."));
    m_engine->setPreviewConsumer(nullptr);
    m_engine->stop();
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
#endif
}

void MainWindow::onStarted()
{
    m_stateLabel->setText(tr("Running"));
}

void MainWindow::onStopped()
{
    m_engine->setPreviewConsumer(nullptr);
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
#endif
    setRunningUi(false);
    m_stateLabel->setText(tr("Idle"));
    m_rateLabel->clear();
}

void MainWindow::onFailed(const QString &message)
{
    m_stateLabel->setText(tr("Error: %1").arg(message));
}

void MainWindow::appendLog(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), message));
}

void MainWindow::updatePidTable(const StreamStats &stats)
{
    uint64_t total = 0;
    for (const auto &p : stats.pidCounts)
        total += p.second;
    m_pidTable->setRowCount(static_cast<int>(stats.pidCounts.size()));
    int row = 0;
    for (const auto &p : stats.pidCounts) {
        auto setCell = [&](int col, const QString &text) {
            QTableWidgetItem *item = m_pidTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                m_pidTable->setItem(row, col, item);
            }
            item->setText(text);
        };
        setCell(0, QStringLiteral("0x%1 (%2)").arg(p.first, 4, 16, QLatin1Char('0')).arg(p.first));
        setCell(1, QLocale().toString(static_cast<qulonglong>(p.second)));
        setCell(2, QStringLiteral("%1 %").arg(total ? 100.0 * static_cast<double>(p.second) / static_cast<double>(total) : 0.0, 0, 'f', 2));
        setCell(3, pidDescription(p.first));
        ++row;
    }
}

void MainWindow::showAbout()
{
    QString features;
#ifdef TBS_HAVE_LINUX_DVB
    features += tr("<li>Linux DVB API backend</li>");
#endif
#ifdef TBS_HAVE_ZMQ
    features += tr("<li>ZeroMQ output</li>");
#endif
#ifdef TBS_HAVE_FFMPEG
    features += tr("<li>FFmpeg video preview</li>");
#endif
    QMessageBox::about(this, tr("About"),
                       tr("<h3>TBS 6903x Receiver %1</h3>"
                          "<p>Tunes TBS DVB-S2/S2X receivers and captures transport streams or BBFrames "
                          "to file and ZeroMQ.</p><p>Built with Qt %2. Features:</p><ul>%3</ul>")
                           .arg(QStringLiteral(TBS_APP_VERSION), QStringLiteral(QT_VERSION_STR), features));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    stopCapture();
    event->accept();
}
