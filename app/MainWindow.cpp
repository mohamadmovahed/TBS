#include "MainWindow.h"

#include "SettingsStore.h"
#include "Theme.h"
#include "panels/OutputPanel.h"
#include "panels/OverviewPanel.h"
#include "panels/PidTable.h"
#include "panels/TuningPanel.h"
#include "video/VideoWidget.h"
#include "widgets/LockChain.h"
#include "widgets/MetricTile.h"
#include "widgets/StatusPill.h"
#ifdef TBS_HAVE_FFMPEG
#include "video/VideoDecoder.h"
#endif

#include <QApplication>
#include <QButtonGroup>
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
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTime>
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
#ifdef TBS_HAVE_FFMPEG
    connect(m_decoder, &VideoDecoder::frameReady, m_video, &VideoWidget::setFrame, Qt::QueuedConnection);
    connect(m_decoder, &VideoDecoder::streamInfo, this, [this](const QString &info) {
        appendLog(tr("Video: %1").arg(info));
        m_video->setInfo(info.section(QLatin1Char(','), 1, 1).trimmed().isEmpty() ? info
                                                                                  : info.section(QLatin1String(" / "), 0, 0));
        m_video->setMessage(info);
    }, Qt::QueuedConnection);
#endif

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
    tabs->addTab(scrollWrap(m_overview), tr("Overview"));
    tabs->addTab(wrapCard(m_pids), tr("PIDs"));
    tabs->addTab(wrapCard(m_log), tr("Log"));

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(top);
    split->addWidget(tabs);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
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
    m_overview->setBbFrameMode(m_config.capture == tbs::CaptureMode::BBFrame);
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    m_tuningDirty = false;
}

void MainWindow::saveSettings()
{
    QSettings s;
    SettingsStore::save(s, currentConfig());
    s.setValue(QStringLiteral("ui/videoPreview"), m_output->videoPreview());
    s.setValue(QStringLiteral("ui/theme"), Theme::mode() == Theme::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
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
    m_controller->start(m_config);
}

void MainWindow::stopCapture()
{
    m_controller->setPreviewEnabled(false);
    m_controller->stop();
#ifdef TBS_HAVE_FFMPEG
    m_decoder->stop();
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
    }
    if (s == tbs::ReceiverState::Running) {
        m_pill->setState(StatusPill::Kind::Warning, tr("SEARCHING"));
    } else if (s == tbs::ReceiverState::Stopped) {
        m_controller->setPreviewEnabled(false);
#ifdef TBS_HAVE_FFMPEG
        m_decoder->stop();
#endif
        setRunningUi(false);
    }
}

void MainWindow::onStatus(const tbs::FrontendStatus &st)
{
    if (!m_running)
        return;
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
