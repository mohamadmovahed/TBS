#include "EpgPanel.h"

#include "../Theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace tbs::bb;

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString serviceKey(const ServiceSchedule &s)
{
    return QStringLiteral("%1.%2.%3").arg(s.onid).arg(s.tsid).arg(s.serviceId);
}

QString serviceTitle(const ServiceSchedule &s)
{
    return s.serviceName.empty() ? QObject::tr("Service %1").arg(s.serviceId) : qs(s.serviceName);
}

QDateTime local(int64_t utc)
{
    return QDateTime::fromSecsSinceEpoch(utc, Qt::UTC).toLocalTime();
}

QString duration(uint32_t sec)
{
    if (sec >= 3600)
        return QStringLiteral("%1 h %2 min").arg(sec / 3600).arg(sec / 60 % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 min").arg(sec / 60);
}

const EventInfo *runningEvent(const ServiceSchedule &s, int64_t now)
{
    for (const EventInfo &e : s.events)
        if (e.startUtc <= now && now < e.startUtc + static_cast<int64_t>(e.durationSec))
            return &e;
    return nullptr;
}

QFrame *card(QWidget *content, const QString &title)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("Card"));
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(12, 4, 12, 10);
    v->addWidget(Theme::sectionTitle(title));
    v->addWidget(content, 1);
    return f;
}

} // namespace

EpgPanel::EpgPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 12, 0, 0);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_stream = new QComboBox;
    m_stream->setVisible(false);
    m_stream->setMinimumWidth(160);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search titles and descriptions"));
    m_search->setClearButtonEnabled(true);
    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("Muted"));
    m_export = new QPushButton(tr("Export XMLTV…"));
    m_export->setObjectName(QStringLiteral("Ghost"));
    m_export->setToolTip(tr("Save the EPG of all services as an XMLTV file (for media centres and EPG tools)"));
    top->addWidget(m_stream);
    top->addWidget(m_search, 1);
    top->addWidget(m_summary);
    top->addWidget(m_export);
    v->addLayout(top);

    m_services = new QListWidget;
    m_services->setToolTip(tr("Double-click to watch the service"));
    m_events = new QTreeWidget;
    m_events->setColumnCount(4);
    m_events->setHeaderLabels({tr("Time"), tr("Duration"), tr("Programme"), tr("Genre")});
    m_events->setRootIsDecorated(false);
    m_events->setUniformRowHeights(true);
    m_events->header()->setStretchLastSection(false);
    m_events->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_events->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_events->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_events->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_details = new QTextBrowser;
    m_details->setOpenLinks(false);
    m_details->setFrameShape(QFrame::NoFrame);

    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(card(m_events, tr("Programmes")));
    right->addWidget(card(m_details, tr("Details")));
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 1);
    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(card(m_services, tr("Services")));
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setChildrenCollapsible(false);

    auto *empty = new QLabel(tr("No EPG yet. Programme information appears here when the stream carries EIT "
                                "tables; the full schedule can take a minute to arrive."));
    empty->setObjectName(QStringLiteral("Hint"));
    empty->setWordWrap(true);
    empty->setAlignment(Qt::AlignCenter);
    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(split);
    v->addWidget(m_stack, 1);

    connect(m_stream, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        m_scrolledToNow = false;
        rebuildServices();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildEvents(); });
    connect(m_services, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const QString key = m_services->item(row)->data(Qt::UserRole).toString();
        if (key != m_selectedKey) {
            m_selectedKey = key;
            m_scrolledToNow = false;
            rebuildEvents();
        }
    });
    connect(m_services, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (const ServiceSchedule *s = currentService())
            emit watchRequested(currentIsi(), s->serviceId, -1);
    });
    connect(m_events, &QTreeWidget::currentItemChanged, this, [this]() { showDetails(); });
    connect(m_export, &QPushButton::clicked, this, &EpgPanel::exportXmltv);
    reset();
}

void EpgPanel::reset()
{
    m_data.clear();
    m_streamUtc.clear();
    m_streamUtcAt.clear();
    m_selectedKey.clear();
    m_scrolledToNow = false;
    m_stream->clear();
    m_stream->setVisible(false);
    m_services->clear();
    m_events->clear();
    m_details->clear();
    m_summary->clear();
    m_export->setEnabled(false);
    m_stack->setCurrentIndex(0);
}

int EpgPanel::currentIsi() const
{
    return m_stream->count() ? m_stream->currentData().toInt() : (m_data.isEmpty() ? -1 : m_data.firstKey());
}

const ServiceSchedule *EpgPanel::currentService() const
{
    const auto it = m_data.find(currentIsi());
    if (it == m_data.end())
        return nullptr;
    for (const ServiceSchedule &s : it.value())
        if (serviceKey(s) == m_selectedKey)
            return &s;
    return nullptr;
}

int64_t EpgPanel::nowUtc() const
{
    const int isi = currentIsi();
    // Stream time (TDT) advanced by the local clock since it was received
    if (m_streamUtc.value(isi, 0) > 0)
        return m_streamUtc.value(isi) + (QDateTime::currentMSecsSinceEpoch() - m_streamUtcAt.value(isi)) / 1000;
    return QDateTime::currentSecsSinceEpoch();
}

void EpgPanel::setSchedules(int isi, const std::vector<ServiceSchedule> &schedules, int64_t streamUtc)
{
    const bool newStream = !m_data.contains(isi);
    m_data[isi] = schedules;
    if (streamUtc > 0) {
        m_streamUtc[isi] = streamUtc;
        m_streamUtcAt[isi] = QDateTime::currentMSecsSinceEpoch();
    }
    if (newStream)
        rebuildStreams();
    rebuildServices();
}

void EpgPanel::rebuildStreams()
{
    QSignalBlocker block(m_stream);
    const int current = currentIsi();
    m_stream->clear();
    for (auto it = m_data.cbegin(); it != m_data.cend(); ++it)
        m_stream->addItem(it.key() < 0 ? tr("Transport stream") : tr("ISI %1").arg(it.key()), it.key());
    const int idx = m_stream->findData(current);
    m_stream->setCurrentIndex(idx >= 0 ? idx : 0);
    m_stream->setVisible(m_data.size() > 1);
}

void EpgPanel::rebuildServices()
{
    const auto it = m_data.find(currentIsi());
    size_t total = 0;
    for (const auto &list : m_data)
        for (const ServiceSchedule &s : list)
            total += s.events.size();
    m_export->setEnabled(total > 0);
    if (it == m_data.end() || it.value().empty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    const auto &list = it.value();
    m_summary->setText(tr("%n service(s)", nullptr, static_cast<int>(list.size())) + QStringLiteral(" · ")
                       + tr("%n programme(s)", nullptr, static_cast<int>(total)));

    const int64_t now = nowUtc();
    QSignalBlocker block(m_services);
    m_services->clear();
    int selectRow = -1;
    for (const ServiceSchedule &s : list) {
        QString text = serviceTitle(s);
        if (!s.actual)
            text += tr("  (other TS)");
        if (const EventInfo *e = runningEvent(s, now))
            text += QStringLiteral("\n") + local(e->startUtc).toString(QStringLiteral("HH:mm ")) + qs(e->name);
        auto *item = new QListWidgetItem(text, m_services);
        item->setData(Qt::UserRole, serviceKey(s));
        if (serviceKey(s) == m_selectedKey)
            selectRow = m_services->count() - 1;
    }
    if (selectRow < 0) {
        selectRow = 0;
        m_selectedKey = m_services->item(0)->data(Qt::UserRole).toString();
        m_scrolledToNow = false;
    }
    m_services->setCurrentRow(selectRow);
    rebuildEvents();
}

void EpgPanel::rebuildEvents()
{
    const ServiceSchedule *s = currentService();
    const int keepId = m_events->currentItem() ? m_events->currentItem()->data(0, Qt::UserRole).toInt() : -1;
    const int scroll = m_events->verticalScrollBar()->value();
    QSignalBlocker block(m_events);
    m_events->clear();
    if (!s) {
        m_details->clear();
        return;
    }
    const Theme::Tokens &t = Theme::tokens();
    const int64_t now = nowUtc();
    const QString filter = m_search->text().trimmed();
    QDate day;
    QTreeWidgetItem *nowItem = nullptr;
    QTreeWidgetItem *keepItem = nullptr;
    QFont dayFont = m_events->font();
    dayFont.setBold(true);
    for (const EventInfo &e : s->events) {
        if (!filter.isEmpty() && !qs(e.name).contains(filter, Qt::CaseInsensitive)
            && !qs(e.text).contains(filter, Qt::CaseInsensitive)
            && !qs(e.extendedText).contains(filter, Qt::CaseInsensitive))
            continue;
        const QDateTime start = local(e.startUtc);
        if (start.date() != day) {
            day = start.date();
            auto *h = new QTreeWidgetItem(m_events, {QLocale().toString(day, QLocale::LongFormat)});
            h->setFirstColumnSpanned(true);
            h->setFont(0, dayFont);
            h->setForeground(0, t.muted);
            h->setFlags(Qt::ItemIsEnabled);
            h->setData(0, Qt::UserRole, -1);
        }
        QString title = qs(e.name);
        if (!e.text.empty() && e.text != e.name)
            title += QStringLiteral("  ·  ") + qs(e.text);
        if (e.parentalAge > 0)
            title += QStringLiteral("  [%1+]").arg(e.parentalAge);
        auto *item = new QTreeWidgetItem(m_events, {start.toString(QStringLiteral("HH:mm")), duration(e.durationSec),
                                                   title, qs(e.genre)});
        item->setData(0, Qt::UserRole, e.eventId);
        item->setToolTip(2, qs(e.name));
        const bool running = e.startUtc <= now && now < e.startUtc + static_cast<int64_t>(e.durationSec);
        if (running) {
            QFont f = item->font(0);
            f.setBold(true);
            for (int c = 0; c < 4; ++c) {
                item->setFont(c, f);
                item->setForeground(c, t.accent);
            }
            item->setText(0, QStringLiteral("● ") + item->text(0));
            nowItem = item;
        } else if (e.startUtc + static_cast<int64_t>(e.durationSec) <= now) {
            for (int c = 0; c < 4; ++c)
                item->setForeground(c, t.muted);
        }
        if (e.eventId == keepId)
            keepItem = item;
    }
    if (keepItem) {
        m_events->setCurrentItem(keepItem);
    } else if (nowItem) {
        m_events->setCurrentItem(nowItem);
    }
    if (!m_scrolledToNow && nowItem) {
        m_events->scrollToItem(nowItem, QAbstractItemView::PositionAtTop);
        m_scrolledToNow = true;
    } else {
        m_events->verticalScrollBar()->setValue(scroll);
    }
    showDetails();
}

void EpgPanel::showDetails()
{
    const ServiceSchedule *s = currentService();
    const QTreeWidgetItem *item = m_events->currentItem();
    if (!s || !item || item->data(0, Qt::UserRole).toInt() < 0) {
        m_details->clear();
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    const EventInfo *e = nullptr;
    for (const EventInfo &x : s->events)
        if (x.eventId == id)
            e = &x;
    if (!e)
        return;
    const Theme::Tokens &t = Theme::tokens();
    const QDateTime start = local(e->startUtc);
    QString html = QStringLiteral("<div style='font-size:15px; font-weight:600'>%1</div>").arg(qs(e->name).toHtmlEscaped());
    QStringList meta;
    meta << QLocale().toString(start, QStringLiteral("ddd d MMM HH:mm")) + QStringLiteral("–")
                + start.addSecs(e->durationSec).toString(QStringLiteral("HH:mm"));
    meta << duration(e->durationSec);
    if (!e->genre.empty())
        meta << qs(e->genre);
    if (e->parentalAge > 0)
        meta << tr("from %1 years%2").arg(e->parentalAge).arg(e->ratingCountry.empty() ? QString() : QStringLiteral(" (%1)").arg(qs(e->ratingCountry)));
    if (!e->language.empty())
        meta << qs(e->language);
    if (e->scrambled)
        meta << tr("scrambled");
    meta << tr("event %1").arg(e->eventId);
    html += QStringLiteral("<div style='color:%1'>%2</div>").arg(t.muted.name(), meta.join(QStringLiteral(" · ")).toHtmlEscaped());
    if (!e->text.empty())
        html += QStringLiteral("<p>%1</p>").arg(qs(e->text).toHtmlEscaped());
    if (!e->extendedText.empty())
        html += QStringLiteral("<p>%1</p>").arg(qs(e->extendedText).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    if (!e->items.empty()) {
        html += QStringLiteral("<table cellspacing=0 cellpadding=2>");
        for (const auto &it : e->items)
            html += QStringLiteral("<tr><td style='color:%1; padding-right:12px'>%2</td><td>%3</td></tr>")
                        .arg(t.muted.name(), qs(it.first).toHtmlEscaped(), qs(it.second).toHtmlEscaped());
        html += QStringLiteral("</table>");
    }
    // Right-to-left scripts (Persian, Arabic) render with the right alignment
    if (qs(e->name).isRightToLeft())
        html = QStringLiteral("<div dir='rtl'>") + html + QStringLiteral("</div>");
    m_details->setHtml(html);
}

void EpgPanel::exportXmltv()
{
    std::vector<ServiceSchedule> all;
    for (const auto &list : m_data)
        all.insert(all.end(), list.begin(), list.end());
    const QString path = QFileDialog::getSaveFileName(this, tr("Export EPG"), QStringLiteral("epg.xml"),
                                                      tr("XMLTV (*.xml);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    const std::string doc = formatXmltv(all);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || f.write(doc.data(), static_cast<qint64>(doc.size())) != static_cast<qint64>(doc.size())) {
        QMessageBox::warning(this, tr("Export EPG"), tr("Cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
}
