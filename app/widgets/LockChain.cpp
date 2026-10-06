#include "LockChain.h"

#include "../Theme.h"

#include <QPainter>

LockChain::LockChain(QWidget *parent)
    : QWidget(parent)
    , m_names({tr("Signal"), tr("Carrier"), tr("FEC"), tr("Sync"), tr("Lock")})
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LockChain::setFlags(uint32_t flags)
{
    if (flags == m_flags)
        return;
    m_flags = flags;
    update();
}

QSize LockChain::sizeHint() const
{
    return QSize(420, 46);
}

QSize LockChain::minimumSizeHint() const
{
    return QSize(260, 46);
}

void LockChain::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = m_names.size();
    const double margin = 30;
    const double step = (width() - 2 * margin) / (n - 1);
    const double y = 14;
    const bool locked = m_flags & 0x10;

    for (int i = 0; i < n - 1; ++i) {
        const bool on = (m_flags & (1u << (i + 1))) && (m_flags & (1u << i));
        p.setPen(QPen(on ? (locked ? t.good : t.accent) : t.hairline, 2));
        p.drawLine(QPointF(margin + i * step + 8, y), QPointF(margin + (i + 1) * step - 8, y));
    }
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    for (int i = 0; i < n; ++i) {
        const bool on = m_flags & (1u << i);
        const QPointF c(margin + i * step, y);
        const QColor col = on ? (locked ? t.good : t.accent) : t.hairline;
        p.setPen(QPen(col, 2));
        p.setBrush(on ? col : t.surface);
        p.drawEllipse(c, 6, 6);
        p.setPen(on ? t.ink : t.muted);
        p.drawText(QRectF(c.x() - 40, y + 10, 80, 18), Qt::AlignHCenter | Qt::AlignTop, m_names[i]);
    }
}
