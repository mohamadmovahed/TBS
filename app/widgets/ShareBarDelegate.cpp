#include "ShareBarDelegate.h"

#include "../Theme.h"

#include <QPainter>
#include <algorithm>

void ShareBarDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const Theme::Tokens &t = Theme::tokens();
    const double share = std::clamp(index.data(kShareRole).toDouble(), 0.0, 1.0);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = opt.rect.adjusted(8, 0, -8, 0);
    const int textW = 64;
    const QRectF track(r.left(), r.center().y() - 3, std::max(10, r.width() - textW), 6);
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
