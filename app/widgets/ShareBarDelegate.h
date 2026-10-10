#pragma once

#include <QStyledItemDelegate>

// Draws a share (0..1, stored under ShareBarDelegate::kShareRole) as a thin meter with the percentage.
class ShareBarDelegate : public QStyledItemDelegate
{
public:
    static constexpr int kShareRole = Qt::UserRole + 1;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};
