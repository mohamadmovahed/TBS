#include "ServicesPanel.h"

#include "../Theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

using namespace tbs::bb;

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString hexId(unsigned v)
{
    return QStringLiteral("0x%1 (%2)").arg(v, 4, 16, QLatin1Char('0')).arg(v);
}

QString eventText(const EventInfo &e)
{
    if (!e.valid)
        return QString();
    QString when;
    if (e.startUtc > 0) {
        const QDateTime start = QDateTime::fromSecsSinceEpoch(e.startUtc, Qt::UTC).toLocalTime();
        when = start.toString(QStringLiteral("HH:mm"));
        if (e.durationSec)
            when += QStringLiteral("–") + start.addSecs(e.durationSec).toString(QStringLiteral("HH:mm"));
        when += QStringLiteral("  ");
    }
    return when + qs(e.name);
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

ServicesPanel::ServicesPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 12, 0, 0);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_stream = new QComboBox;
    m_stream->setVisible(false);
    m_stream->setMinimumWidth(180);
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(true);
    top->addWidget(m_stream);
    top->addWidget(m_summary, 1);
    v->addLayout(top);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("Name"), tr("ID / PID"), tr("Type"), tr("Details")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->resizeSection(0, 280);
    m_tree->header()->resizeSection(1, 120);
    m_tree->header()->resizeSection(2, 170);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setToolTip(tr("Double-click a service, or one of its audio streams, to watch it"));
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        // Keys: svc/<program> and svc/<program>/es<pid>
        const QStringList parts = item->data(0, Qt::UserRole).toString().split(QLatin1Char('/'));
        if (parts.size() < 2 || parts[0] != QLatin1String("svc"))
            return;
        const int program = parts[1].toInt();
        int audio = -1;
        if (parts.size() == 3 && parts[2].startsWith(QLatin1String("es")) && item->text(2) == QLatin1String("Audio"))
            audio = parts[2].mid(2).toInt();
        const int idx = m_stream->currentIndex();
        const int isi = idx >= 0 && idx < static_cast<int>(m_streamIsis.size()) ? m_streamIsis[static_cast<size_t>(idx)] : -1;
        if (program > 0)
            emit watchRequested(isi, program, audio);
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *i) { m_expanded.insert(i->data(0, Qt::UserRole).toString()); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *i) { m_expanded.remove(i->data(0, Qt::UserRole).toString()); });

    m_tables = new QTableWidget(0, 6);
    m_tables->setHorizontalHeaderLabels({tr("Table"), tr("PID"), tr("Version"), tr("Instances"), tr("Sections"), tr("CRC errors")});
    m_tables->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tables->horizontalHeader()->setStretchLastSection(true);
    m_tables->verticalHeader()->setVisible(false);
    m_tables->verticalHeader()->setDefaultSectionSize(26);
    m_tables->setShowGrid(false);
    m_tables->setAlternatingRowColors(true);
    m_tables->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tables->setSelectionMode(QAbstractItemView::NoSelection);
    m_tables->setFrameShape(QFrame::NoFrame);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(card(m_tree, tr("Services & networks")));
    split->addWidget(card(m_tables, tr("Tables")));
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    auto *empty = new QLabel(tr("No transport stream analysed yet.\nTS content appears here while capturing a TS, "
                                "or BBFrames that carry a TS."));
    empty->setObjectName(QStringLiteral("Muted"));
    empty->setAlignment(Qt::AlignCenter);
    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(split);
    v->addWidget(m_stack, 1);

    connect(m_stream, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { m_signature = 0; });
}

void ServicesPanel::reset()
{
    m_tree->clear();
    m_tables->setRowCount(0);
    m_summary->clear();
    m_stream->clear();
    m_stream->setVisible(false);
    m_streamIsis.clear();
    m_signature = 0;
    m_stack->setCurrentIndex(0);
}

QTreeWidgetItem *ServicesPanel::addItem(QTreeWidgetItem *parent, const QString &key, const QStringList &columns)
{
    auto *item = parent ? new QTreeWidgetItem(parent, columns) : new QTreeWidgetItem(m_tree, columns);
    item->setData(0, Qt::UserRole, key);
    for (int c = 0; c < columns.size(); ++c)
        item->setToolTip(c, columns[c]);
    return item;
}

void ServicesPanel::updateSnapshot(const ProcessorSnapshot &snap)
{
    // TS streams of this capture (one per ISI on a multistream carrier)
    std::vector<int> isis;
    std::vector<const StreamReport *> reports;
    for (const auto &s : snap.streams) {
        if (s.content == StreamContent::TransportStream || s.content == StreamContent::TsInput) {
            isis.push_back(s.isi);
            reports.push_back(&s);
        }
    }
    if (isis != m_streamIsis) {
        const int previous = m_stream->currentIndex() >= 0 && m_stream->currentIndex() < static_cast<int>(m_streamIsis.size())
            ? m_streamIsis[static_cast<size_t>(m_stream->currentIndex())]
            : -100;
        m_streamIsis = isis;
        const QSignalBlocker block(m_stream);
        m_stream->clear();
        int select = 0;
        for (size_t i = 0; i < isis.size(); ++i) {
            m_stream->addItem(isis[i] < 0 ? tr("Transport stream") : tr("ISI %1 · transport stream").arg(isis[i]));
            if (isis[i] == previous)
                select = static_cast<int>(i);
        }
        m_stream->setCurrentIndex(select);
        m_stream->setVisible(isis.size() > 1);
        m_signature = 0;
    }
    if (reports.empty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    const int idx = std::max(0, m_stream->currentIndex());
    const StreamReport &sr = *reports[static_cast<size_t>(std::min<int>(idx, static_cast<int>(reports.size()) - 1))];
    const TsReport &r = sr.ts;

    // Summary chips
    const Theme::Tokens &t = Theme::tokens();
    QString network;
    for (const auto &n : r.networks)
        if (n.actual && !n.name.empty())
            network = qs(n.name);
    size_t actualServices = 0;
    for (const auto &s : r.services)
        actualServices += s.actual ? 1 : 0;
    QStringList parts;
    auto chip = [&](const QString &k, const QString &val) {
        parts << QStringLiteral("<span style='color:%1'>%2</span> <b>%3</b>").arg(t.muted.name(), k, val.toHtmlEscaped());
    };
    chip(tr("TSID"), r.patSeen ? QString::number(r.tsid) : QStringLiteral("–"));
    chip(tr("ONID"), r.onid ? QString::number(r.onid) : QStringLiteral("–"));
    if (!network.isEmpty())
        chip(tr("Network"), network);
    chip(tr("Services"), QString::number(std::max(actualServices, r.programs.size())));
    if (r.hasTime)
        chip(tr("Stream time"), QDateTime::fromSecsSinceEpoch(r.utcTime, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'")));
    chip(tr("Sections"), QLocale().toString(static_cast<qulonglong>(r.sections)));
    parts << QStringLiteral("<span style='color:%1'>%2</span> <b>%3%4</b>")
                 .arg(t.muted.name(), tr("CRC errors"),
                      r.crcErrors ? QStringLiteral("<span style='color:%1'>● </span>").arg(t.critical.name()) : QString())
                 .arg(r.crcErrors);
    if (r.scheduleEvents)
        chip(tr("EPG events"), QLocale().toString(static_cast<qulonglong>(r.scheduleEvents)));
    m_summary->setText(parts.join(QStringLiteral(" &nbsp;·&nbsp; ")));

    // Rebuild the tree only when its content changed
    std::string sig;
    for (const auto &p : r.programs) {
        sig += std::to_string(p.programNumber) + ':' + std::to_string(p.pmtVersion) + ':' + std::to_string(p.streams.size());
        for (const auto &e : p.streams)
            sig += ':' + std::to_string(e.pid) + e.codec + e.language;
    }
    for (const auto &s : r.services)
        sig += '|' + std::to_string(s.serviceId) + s.name + s.provider + std::to_string(s.type) + (s.scrambled ? "s" : "")
            + std::to_string(s.present.eventId) + s.present.name + std::to_string(s.following.eventId) + s.following.name;
    for (const auto &n : r.networks) {
        sig += '#' + std::to_string(n.networkId) + n.name + std::to_string(n.version);
        for (const auto &tp : n.transports)
            sig += tp.delivery + std::to_string(tp.serviceIds.size());
    }
    for (const auto &b : r.bouquets)
        sig += '%' + b.name + std::to_string(b.transports.size());
    sig += '!' + std::to_string(r.emm.size());
    const size_t signature = std::hash<std::string>()(sig) ^ static_cast<size_t>(idx + 1);
    if (signature != m_signature) {
        m_signature = signature;
        rebuildTree(r);
    }
    updateTables(r);
}

void ServicesPanel::rebuildTree(const TsReport &r)
{
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    const Theme::Tokens &t = Theme::tokens();

    auto serviceFor = [&](uint16_t program) -> const ServiceInfo * {
        for (const auto &s : r.services)
            if (s.actual && s.serviceId == program && (!r.patSeen || s.tsid == r.tsid))
                return &s;
        return nullptr;
    };

    // ---- Services of this transport stream ----
    std::vector<uint16_t> listed;
    auto *services = addItem(nullptr, QStringLiteral("services"), {tr("Services")});
    QFont bold = services->font(0);
    bold.setBold(true);
    auto addService = [&](uint16_t number, const ProgramInfo *prog, const ServiceInfo *svc) {
        const QString name = svc && !svc->name.empty() ? qs(svc->name) : tr("Program %1").arg(number);
        QStringList details;
        if (svc && !svc->provider.empty())
            details << qs(svc->provider);
        if (svc)
            details << (svc->scrambled ? tr("scrambled") : tr("free-to-air"));
        if (prog && !prog->ca.empty())
            details << tr("CA: %1").arg(qs(prog->ca.front().systemName));
        if (svc && svc->present.valid)
            details << tr("now: %1").arg(eventText(svc->present));
        const QString key = QStringLiteral("svc/%1").arg(number);
        auto *item = addItem(services, key,
                             {name, hexId(number), svc ? qs(svc->typeName) : tr("(no SDT entry)"), details.join(QStringLiteral(" · "))});
        item->setFont(0, bold);
        if (prog) {
            addItem(item, key + QStringLiteral("/pmt"),
                    {tr("PMT"), hexId(prog->pmtPid), prog->pmtSeen ? tr("version %1").arg(prog->pmtVersion) : tr("not received"),
                     QString()});
            if (prog->pcrPid != 0x1FFF)
                addItem(item, key + QStringLiteral("/pcr"), {tr("PCR"), hexId(prog->pcrPid), QString(), QString()});
            for (const auto &es : prog->streams) {
                QStringList d;
                if (!es.language.empty())
                    d << qs(es.language);
                d << tr("stream type 0x%1").arg(es.streamType, 2, 16, QLatin1Char('0'));
                for (const auto &ca : es.ca)
                    d << tr("ECM %1 on 0x%2").arg(qs(ca.systemName)).arg(ca.pid, 4, 16, QLatin1Char('0'));
                addItem(item, key + QStringLiteral("/es%1").arg(es.pid),
                        {qs(es.kind) + QStringLiteral(" · ") + qs(es.codec), hexId(es.pid), qs(es.kind), d.join(QStringLiteral(" · "))});
            }
            for (const auto &ca : prog->ca)
                addItem(item, key + QStringLiteral("/ecm%1").arg(ca.pid),
                        {tr("ECM"), hexId(ca.pid), qs(ca.systemName), tr("CA system 0x%1").arg(ca.systemId, 4, 16, QLatin1Char('0'))});
        }
        if (svc) {
            for (const EventInfo *e : {&svc->present, &svc->following}) {
                if (!e->valid)
                    continue;
                const bool now = e == &svc->present;
                addItem(item, key + (now ? QStringLiteral("/now") : QStringLiteral("/next")),
                        {now ? tr("Now") : tr("Next"), QString::number(e->eventId), eventText(*e), qs(e->text)});
            }
        }
        listed.push_back(number);
    };
    for (const auto &p : r.programs)
        addService(p.programNumber, &p, serviceFor(p.programNumber));
    for (const auto &s : r.services)
        if (s.actual && std::find(listed.begin(), listed.end(), s.serviceId) == listed.end())
            addService(s.serviceId, nullptr, &s);
    services->setText(0, tr("Services (%1)").arg(services->childCount()));
    services->setFont(0, bold);

    // ---- Other transport streams (SDT other) ----
    std::map<std::pair<uint16_t, uint16_t>, std::vector<const ServiceInfo *>> others;
    for (const auto &s : r.services)
        if (!s.actual)
            others[{s.onid, s.tsid}].push_back(&s);
    if (!others.empty()) {
        auto *grp = addItem(nullptr, QStringLiteral("other"), {tr("Other transport streams (%1)").arg(others.size())});
        grp->setFont(0, bold);
        for (const auto &o : others) {
            const QString key = QStringLiteral("other/%1/%2").arg(o.first.first).arg(o.first.second);
            auto *ts = addItem(grp, key, {tr("TS %1").arg(o.first.second), tr("ONID %1").arg(o.first.first),
                                          tr("%1 services").arg(o.second.size()), QString()});
            for (const ServiceInfo *s : o.second)
                addItem(ts, key + QStringLiteral("/%1").arg(s->serviceId),
                        {qs(s->name), hexId(s->serviceId), qs(s->typeName),
                         QStringList({qs(s->provider), s->present.valid ? tr("now: %1").arg(eventText(s->present)) : QString()})
                             .join(QStringLiteral(" · "))});
        }
    }

    // ---- Networks ----
    if (!r.networks.empty()) {
        auto *grp = addItem(nullptr, QStringLiteral("networks"), {tr("Networks (%1)").arg(r.networks.size())});
        grp->setFont(0, bold);
        for (const auto &n : r.networks) {
            const QString key = QStringLiteral("net/%1/%2").arg(n.actual).arg(n.networkId);
            auto *ni = addItem(grp, key, {n.name.empty() ? tr("Network %1").arg(n.networkId) : qs(n.name), hexId(n.networkId),
                                          n.actual ? tr("NIT actual") : tr("NIT other"),
                                          tr("%1 transport streams · version %2").arg(n.transports.size()).arg(n.version)});
            for (const auto &tsi : n.transports)
                addItem(ni, key + QStringLiteral("/%1/%2").arg(tsi.onid).arg(tsi.tsid),
                        {tr("TS %1 / ONID %2").arg(tsi.tsid).arg(tsi.onid), hexId(tsi.tsid), tr("%1 services").arg(tsi.serviceIds.size()),
                         qs(tsi.delivery)});
        }
    }

    // ---- Bouquets ----
    if (!r.bouquets.empty()) {
        auto *grp = addItem(nullptr, QStringLiteral("bouquets"), {tr("Bouquets (%1)").arg(r.bouquets.size())});
        grp->setFont(0, bold);
        for (const auto &b : r.bouquets)
            addItem(grp, QStringLiteral("bat/%1").arg(b.bouquetId),
                    {b.name.empty() ? tr("Bouquet %1").arg(b.bouquetId) : qs(b.name), hexId(b.bouquetId), tr("BAT"),
                     tr("%1 transport streams").arg(b.transports.size())});
    }

    // ---- Conditional access ----
    if (!r.emm.empty()) {
        auto *grp = addItem(nullptr, QStringLiteral("cat"), {tr("Conditional access (CAT)")});
        grp->setFont(0, bold);
        for (const auto &e : r.emm)
            addItem(grp, QStringLiteral("cat/%1").arg(e.pid),
                    {tr("EMM"), hexId(e.pid), qs(e.systemName), tr("CA system 0x%1").arg(e.systemId, 4, 16, QLatin1Char('0'))});
    }

    // Restore expansion: remembered state, plus new groups and services open by default
    std::function<void(QTreeWidgetItem *, int)> restore = [&](QTreeWidgetItem *item, int depth) {
        const QString key = item->data(0, Qt::UserRole).toString();
        const bool fresh = !m_known.contains(key);
        m_known.insert(key);
        if (m_expanded.contains(key) || (fresh && depth == 0)) {
            item->setExpanded(true);
            m_expanded.insert(key);
        }
        for (int i = 0; i < item->childCount(); ++i)
            restore(item->child(i), depth + 1);
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        restore(m_tree->topLevelItem(i), 0);
    Q_UNUSED(t);
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(scroll);
}

void ServicesPanel::updateTables(const TsReport &r)
{
    m_tables->setRowCount(static_cast<int>(r.tables.size()));
    const Theme::Tokens &t = Theme::tokens();
    int row = 0;
    for (const auto &tb : r.tables) {
        auto cell = [&](int col, const QString &text, bool right = false) {
            QTableWidgetItem *item = m_tables->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                m_tables->setItem(row, col, item);
            }
            item->setText(text);
            if (right)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        cell(0, qs(tb.name));
        cell(1, QStringLiteral("0x%1").arg(tb.pid, 4, 16, QLatin1Char('0')));
        cell(2, tb.version >= 0 ? QString::number(tb.version) : QStringLiteral("–"), true);
        cell(3, tb.instances ? QString::number(tb.instances) : QStringLiteral("–"), true);
        cell(4, QLocale().toString(static_cast<qulonglong>(tb.sections)), true);
        QTableWidgetItem *crc = cell(5, tb.crcErrors ? QStringLiteral("● %1").arg(tb.crcErrors) : QStringLiteral("0"), true);
        crc->setForeground(tb.crcErrors ? t.critical : t.ink);
        ++row;
    }
}
