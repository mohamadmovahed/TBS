#include "PidTable.h"

#include "../Theme.h"
#include "../widgets/ShareBarDelegate.h"

#include <tbsdvb/BbFrameExtractor.h>

#include <QHeaderView>
#include <QLocale>

namespace {

QString describe(uint16_t pid)
{
    if (tbs::BbFrameExtractor::isBbFramePid(pid))
        return QObject::tr("BBFrame carrier");
    switch (pid) {
    case 0x0000:
        return QStringLiteral("PAT");
    case 0x0001:
        return QStringLiteral("CAT");
    case 0x0010:
        return QStringLiteral("NIT");
    case 0x0011:
        return QStringLiteral("SDT / BAT");
    case 0x0012:
        return QStringLiteral("EIT");
    case 0x0014:
        return QStringLiteral("TDT / TOT");
    case 0x1FFF:
        return QObject::tr("Null packets");
    default:
        return QString();
    }
}

} // namespace

PidTable::PidTable(QWidget *parent)
    : QTableWidget(0, 4, parent)
{
    setHorizontalHeaderLabels({tr("PID"), tr("Packets"), tr("Share"), tr("Content")});
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(30);
    setShowGrid(false);
    setAlternatingRowColors(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setItemDelegateForColumn(2, new ShareBarDelegate(this));
}

void PidTable::reset()
{
    setRowCount(0);
    m_usage.clear();
}

void PidTable::setPidUsage(const std::map<uint16_t, std::string> &usage)
{
    m_usage = usage;
}

void PidTable::updateStats(const tbs::StreamStats &st)
{
    uint64_t total = 0;
    for (const auto &p : st.pidCounts)
        total += p.second;
    setRowCount(static_cast<int>(st.pidCounts.size()));
    int row = 0;
    for (const auto &p : st.pidCounts) {
        auto cell = [&](int col) {
            QTableWidgetItem *item = this->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                setItem(row, col, item);
            }
            return item;
        };
        cell(0)->setText(QStringLiteral("0x%1  ·  %2").arg(p.first, 4, 16, QLatin1Char('0')).arg(p.first));
        cell(1)->setText(QLocale().toString(static_cast<qulonglong>(p.second)));
        cell(1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cell(2)->setData(ShareBarDelegate::kShareRole, total ? static_cast<double>(p.second) / static_cast<double>(total) : 0.0);
        const auto u = m_usage.find(p.first);
        cell(3)->setText(u != m_usage.end() ? QString::fromStdString(u->second) : describe(p.first));
        ++row;
    }
    viewport()->update();
}
