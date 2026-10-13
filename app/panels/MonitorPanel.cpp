#include "MonitorPanel.h"

#include "../Theme.h"
#include "../widgets/HistoryChart.h"
#include "FormHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

using namespace tbs;

namespace {

QFrame *card(QWidget *content, QWidget *title)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("Card"));
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(12, 4, 12, 10);
    v->addWidget(title);
    v->addWidget(content, 1);
    return f;
}

QString timeText(qint64 ms)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    return t.date() == QDate::currentDate() ? t.toString(QStringLiteral("HH:mm:ss"))
                                            : t.toString(QStringLiteral("d MMM HH:mm:ss"));
}

enum Metric { CnrDb, LevelDbm, InputMbps, PayloadMbps, CcErrors, BbCrcErrors };

} // namespace

MonitorPanel::MonitorPanel(Monitor *monitor, QWidget *parent)
    : QWidget(parent)
    , m_monitor(monitor)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 12, 0, 0);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_metric = new QComboBox;
    m_metric->addItem(tr("C/N"), CnrDb);
    m_metric->addItem(tr("Signal level"), LevelDbm);
    m_metric->addItem(tr("Input bitrate"), InputMbps);
    m_metric->addItem(tr("Payload bitrate"), PayloadMbps);
    m_metric->addItem(tr("CC errors"), CcErrors);
    m_metric->addItem(tr("BBFrame CRC errors"), BbCrcErrors);
    m_metric->setCurrentIndex(m_metric->findData(InputMbps)); // always available (file replay has no C/N)
    m_range = new QComboBox;
    m_range->addItem(tr("Last 10 minutes"), 600);
    m_range->addItem(tr("Last hour"), 3600);
    m_range->addItem(tr("Last 6 hours"), 6 * 3600);
    m_range->addItem(tr("Last 24 hours"), 24 * 3600);
    auto *settings = new QPushButton(tr("Alarm settings…"));
    settings->setObjectName(QStringLiteral("Ghost"));
    auto *csv = new QPushButton(tr("Export CSV…"));
    csv->setObjectName(QStringLiteral("Ghost"));
    csv->setToolTip(tr("Save the history of the selected time range, one line per second"));
    top->addWidget(keyLabel(tr("Show")));
    top->addWidget(m_metric);
    top->addWidget(m_range);
    top->addStretch(1);
    top->addWidget(settings);
    top->addWidget(csv);
    v->addLayout(top);

    m_chart = new HistoryChart;
    m_chartTitle = Theme::sectionTitle(tr("History"));
    m_alarms = new QTreeWidget;
    m_alarms->setHeaderLabels({tr("Alarm"), tr("State"), tr("Since"), tr("Raised"), tr("Details")});
    m_alarms->setRootIsDecorated(false);
    m_alarms->header()->setStretchLastSection(true);
    for (int c = 0; c < 4; ++c)
        m_alarms->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_events = new QTreeWidget;
    m_events->setHeaderLabels({tr("Time"), tr("Alarm"), tr("Event"), tr("Details")});
    m_events->setRootIsDecorated(false);
    m_events->header()->setStretchLastSection(true);
    for (int c = 0; c < 3; ++c)
        m_events->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    auto *bottom = new QSplitter(Qt::Horizontal);
    bottom->addWidget(card(m_alarms, Theme::sectionTitle(tr("Alarms"))));
    bottom->addWidget(card(m_events, Theme::sectionTitle(tr("Alarm events"))));
    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(card(m_chart, m_chartTitle));
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    v->addWidget(split, 1);

    connect(m_metric, qOverload<int>(&QComboBox::currentIndexChanged), this, &MonitorPanel::refreshChart);
    connect(m_range, qOverload<int>(&QComboBox::currentIndexChanged), this, &MonitorPanel::refreshChart);
    connect(csv, &QPushButton::clicked, this, &MonitorPanel::exportCsv);
    connect(settings, &QPushButton::clicked, this, &MonitorPanel::editSettings);
    refresh();
}

void MonitorPanel::refresh()
{
    const Theme::Tokens &t = Theme::tokens();
    // Alarm table: state with a coloured dot and a word, never colour alone
    const auto alarms = m_monitor->alarms();
    m_alarms->setUpdatesEnabled(false);
    if (m_alarms->topLevelItemCount() != static_cast<int>(alarms.size())) {
        m_alarms->clear();
        for (size_t i = 0; i < alarms.size(); ++i)
            new QTreeWidgetItem(m_alarms);
    }
    for (size_t i = 0; i < alarms.size(); ++i) {
        const Alarm &a = alarms[i];
        QTreeWidgetItem *it = m_alarms->topLevelItem(static_cast<int>(i));
        it->setText(0, tr(alarmTypeTitle(a.type)));
        if (!a.enabled) {
            it->setText(1, QStringLiteral("○  ") + tr("Off"));
            it->setForeground(1, t.muted);
        } else if (a.active) {
            it->setText(1, QStringLiteral("●  ") + tr("ALARM"));
            it->setForeground(1, t.critical);
        } else {
            it->setText(1, QStringLiteral("●  ") + tr("OK"));
            it->setForeground(1, t.good);
        }
        QFont f = it->font(0);
        f.setBold(a.active);
        for (int c = 0; c < 5; ++c)
            it->setFont(c, f);
        it->setText(2, a.sinceMs ? timeText(a.sinceMs) : QStringLiteral("–"));
        it->setText(3, QString::number(a.raisedCount));
        it->setText(4, QString::fromStdString(a.message));
    }
    m_alarms->setUpdatesEnabled(true);

    // Events, newest first
    const auto events = m_monitor->events(300);
    if (events.size() != m_eventCount || (!events.empty() && m_events->topLevelItemCount() == 0)) {
        m_eventCount = events.size();
        m_events->clear();
        for (auto e = events.rbegin(); e != events.rend(); ++e) {
            auto *it = new QTreeWidgetItem(m_events, {timeText(e->timeMs), tr(alarmTypeTitle(e->type)),
                                                     e->raised ? tr("raised") : tr("cleared"),
                                                     QString::fromStdString(e->message)});
            it->setForeground(2, e->raised ? t.critical : t.ink2);
        }
    }
    refreshChart();
}

void MonitorPanel::refreshChart()
{
    const int metric = m_metric->currentData().toInt();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 from = now - static_cast<qint64>(m_range->currentData().toInt()) * 1000;
    const auto samples = m_monitor->history(from);
    QVector<HistoryChart::Point> pts;
    pts.reserve(static_cast<int>(samples.size()));
    for (const MonitorSample &s : samples) {
        double v = std::nan("");
        switch (metric) {
        case CnrDb: v = s.running ? s.cnrDb : std::nan(""); break;
        case LevelDbm: v = s.running ? s.levelDbm : std::nan(""); break;
        case InputMbps: v = s.inputMbps; break;
        case PayloadMbps: v = s.payloadMbps; break;
        case CcErrors: v = s.ccErrors; break;
        case BbCrcErrors: v = s.bbCrcErrors; break;
        }
        pts.push_back({s.timeMs, v});
    }
    // Lock-lost periods as bands
    QVector<HistoryChart::Band> bands;
    qint64 start = 0;
    for (const MonitorSample &s : samples) {
        const bool bad = s.running && !s.locked;
        if (bad && !start)
            start = s.timeMs - 1000;
        if (!bad && start) {
            bands.push_back({start, s.timeMs, tr("no lock")});
            start = 0;
        }
    }
    if (start)
        bands.push_back({start, now, tr("no lock")});

    static const char *const units[] = {"dB", "dBm", "Mbit/s", "Mbit/s", "", ""};
    const bool bars = metric == CcErrors || metric == BbCrcErrors;
    m_chart->setRange(from, now);
    m_chart->setBands(bands);
    m_chart->setSeries(pts, bars ? HistoryChart::Kind::Bars : HistoryChart::Kind::Line,
                       bars ? tr("errors") : QString::fromLatin1(units[metric]), bars ? 0 : 2);
    QString title = m_metric->currentText();
    if (!bars)
        title += QStringLiteral(" (%1)").arg(QString::fromLatin1(units[metric]));
    else
        title += tr(" per interval");
    m_chartTitle->setText(title.toUpper());
}

void MonitorPanel::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export history"),
        QStringLiteral("tbs-history-%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    const qint64 from = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(m_range->currentData().toInt()) * 1000;
    std::string err;
    if (!m_monitor->writeCsv(path.toStdString(), from, &err))
        QMessageBox::warning(this, tr("Export history"), QString::fromStdString(err));
}

void MonitorPanel::editSettings()
{
    AlarmSettingsDialog dlg(m_monitor->config(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_monitor->setConfig(dlg.config());
    emit configChanged(dlg.config());
    refresh();
}

// ---------------------------------------------------------------------------

AlarmSettingsDialog::AlarmSettingsDialog(const MonitorConfig &cfg, QWidget *parent)
    : QDialog(parent)
    , m_cfg(cfg)
{
    setWindowTitle(tr("Alarm settings"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    auto *g = new QGridLayout;
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(8);
    int row = 0;
    auto spin = [](int lo, int hi, int v, const QString &suffix) {
        auto *s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(v);
        s->setSuffix(suffix);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return s;
    };
    auto dspin = [](double lo, double hi, double v, int dec, const QString &suffix) {
        auto *s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setValue(v);
        s->setSuffix(suffix);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return s;
    };
    auto add = [&](QCheckBox *on, QWidget *a, const QString &mid, QWidget *b, const QString &hint) {
        g->addWidget(on, row, 0);
        if (a)
            g->addWidget(a, row, 1);
        if (!mid.isEmpty())
            g->addWidget(keyLabel(mid), row, 2);
        if (b)
            g->addWidget(b, row, 3);
        auto *h = hintLabel(hint);
        g->addWidget(h, row, 4);
        ++row;
    };
    const QString s = tr(" s");
    m_noLock = new QCheckBox(tr("Lock lost"));
    m_noLockHold = spin(0, 3600, cfg.noLockHoldSec, s);
    add(m_noLock, nullptr, tr("for"), m_noLockHold, QString());
    m_lowCnr = new QCheckBox(tr("C/N below"));
    m_cnrMin = dspin(-10, 40, cfg.cnrMinDb, 1, tr(" dB"));
    m_cnrHold = spin(0, 3600, cfg.cnrHoldSec, s);
    add(m_lowCnr, m_cnrMin, tr("for"), m_cnrHold, QString());
    m_lowLevel = new QCheckBox(tr("Level below"));
    m_levelMin = dspin(-120, 0, cfg.levelMinDbm, 1, tr(" dBm"));
    m_levelHold = spin(0, 3600, cfg.levelHoldSec, s);
    add(m_lowLevel, m_levelMin, tr("for"), m_levelHold, QString());
    m_lowRate = new QCheckBox(tr("Bitrate below"));
    m_rateMin = dspin(0, 1000, cfg.bitrateMinMbps, 2, tr(" Mbit/s"));
    m_rateHold = spin(0, 3600, cfg.bitrateHoldSec, s);
    add(m_lowRate, m_rateMin, tr("for"), m_rateHold, QString());
    m_noData = new QCheckBox(tr("No data"));
    m_noDataHold = spin(0, 3600, cfg.noDataHoldSec, s);
    add(m_noData, nullptr, tr("for"), m_noDataHold, QString());
    m_cc = new QCheckBox(tr("CC errors above"));
    m_ccMax = spin(0, 1000000, static_cast<int>(cfg.ccMaxPerWindow), QString());
    m_window = spin(1, 3600, cfg.windowSec, s);
    add(m_cc, m_ccMax, tr("in"), m_window, tr("(error window)"));
    m_crc = new QCheckBox(tr("BBFrame CRC errors above"));
    m_crcMax = spin(0, 1000000, static_cast<int>(cfg.bbCrcMaxPerWindow), QString());
    add(m_crc, m_crcMax, QString(), nullptr, tr("in the error window"));
    m_drops = new QCheckBox(tr("Data dropped"));
    add(m_drops, nullptr, QString(), nullptr, tr("driver overflows or processing too slow"));
    m_output = new QCheckBox(tr("Output errors"));
    add(m_output, nullptr, QString(), nullptr, tr("recording stopped, network send errors"));
    m_noLock->setChecked(cfg.noLock);
    m_lowCnr->setChecked(cfg.lowCnr);
    m_lowLevel->setChecked(cfg.lowLevel);
    m_lowRate->setChecked(cfg.lowBitrate);
    m_noData->setChecked(cfg.noData);
    m_cc->setChecked(cfg.ccErrors);
    m_crc->setChecked(cfg.bbCrcErrors);
    m_drops->setChecked(cfg.drops);
    m_output->setChecked(cfg.outputErrors);
    root->addLayout(g);

    root->addWidget(Theme::sectionTitle(tr("Run on every alarm change")));
    m_command = new QLineEdit(QString::fromStdString(cfg.command));
    m_command->setPlaceholderText(tr("e.g. notify-send TBS {alarm}:{state} {message}"));
    root->addWidget(m_command);
    root->addWidget(hintLabel(tr("Placeholders, shell quoted: <tt>{alarm}</tt> (no-lock, low-cnr, …), <tt>{state}</tt> "
                                 "(raised or cleared), <tt>{message}</tt>, <tt>{time}</tt>. Use it to send mail, call a "
                                 "webhook with curl, or switch to a backup feed.")));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("Primary"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

MonitorConfig AlarmSettingsDialog::config() const
{
    MonitorConfig c = m_cfg;
    c.noLock = m_noLock->isChecked();
    c.noLockHoldSec = m_noLockHold->value();
    c.lowCnr = m_lowCnr->isChecked();
    c.cnrMinDb = m_cnrMin->value();
    c.cnrHoldSec = m_cnrHold->value();
    c.lowLevel = m_lowLevel->isChecked();
    c.levelMinDbm = m_levelMin->value();
    c.levelHoldSec = m_levelHold->value();
    c.lowBitrate = m_lowRate->isChecked();
    c.bitrateMinMbps = m_rateMin->value();
    c.bitrateHoldSec = m_rateHold->value();
    c.noData = m_noData->isChecked();
    c.noDataHoldSec = m_noDataHold->value();
    c.ccErrors = m_cc->isChecked();
    c.ccMaxPerWindow = static_cast<uint32_t>(m_ccMax->value());
    c.windowSec = m_window->value();
    c.bbCrcErrors = m_crc->isChecked();
    c.bbCrcMaxPerWindow = static_cast<uint32_t>(m_crcMax->value());
    c.drops = m_drops->isChecked();
    c.outputErrors = m_output->isChecked();
    c.command = m_command->text().trimmed().toStdString();
    return c;
}
