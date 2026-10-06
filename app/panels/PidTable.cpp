#include "PidTable.h"

#include "../Theme.h"

#include <tbsdvb/BbFrameExtractor.h>

#include <QHeaderView>
#include <QLocale>
#include <QPainter>
#include <QStyledItemDelegate>

namespace {

constexpr int kShareRole = Qt::UserRole + 1;

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

// Meter: track in the hairline colour, fill in the accent, value as text
class ShareDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const Theme::Tokens &t = Theme::tokens();
        const double share = index.data(kShareRole).toDouble();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = opt.rect.adjusted(8, 0, -8, 0);
        const int textW = 64;
        const QRectF track(r.left(), r.center().y() - 3, r.width() - textW, 6);
        p->setPen(Qt::NoPen);
        p->setBrush(t.hairline);
        p->drawRoundedRect(track, 3, 3);
        QRectF fill = track;
        fill.setWidth(std::max(share > 0 ? 3.0 : 0.0, track.width() * share));
        p->setBrush(t.accent);
        p->drawRoundedRect(fill, 3, 3);
        p->setPen(t.ink2);
        p->drawText(QRect(r.right() - textW + 8, r.top(), textW - 8, r.height()), Qt::AlignVCenter | Qt::AlignRight,
                    QStringLiteral("%1 %").arg(share * 100.0, 0, 'f', share < 0.1 ? 2 : 1));
        p->restore();
    }
};

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
    setItemDelegateForColumn(2, new ShareDelegate(this));
}

void PidTable::reset()
{
    setRowCount(0);
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
        cell(2)->setData(kShareRole, total ? static_cast<double>(p.second) / static_cast<double>(total) : 0.0);
        cell(3)->setText(describe(p.first));
        ++row;
    }
    viewport()->update();
}
