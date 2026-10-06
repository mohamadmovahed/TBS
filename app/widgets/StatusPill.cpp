#include "StatusPill.h"

#include "../Theme.h"

#include <QPainter>

StatusPill::StatusPill(QWidget *parent)
    : QWidget(parent)
    , m_text(tr("IDLE"))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void StatusPill::setState(Kind kind, const QString &text)
{
    m_kind = kind;
    m_text = text;
    updateGeometry();
    update();
}

QSize StatusPill::sizeHint() const
{
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::Bold);
    return QSize(QFontMetrics(f).horizontalAdvance(m_text) + 36, 28);
}

void StatusPill::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QColor c;
    switch (m_kind) {
    case Kind::Neutral:
        c = t.muted;
        break;
    case Kind::Good:
        c = t.good;
        break;
    case Kind::Warning:
        c = t.warning;
        break;
    case Kind::Critical:
        c = t.critical;
        break;
    case Kind::Accent:
        c = t.accent;
        break;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = c;
    bg.setAlphaF(0.18);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    p.setBrush(c);
    p.drawEllipse(QPointF(14, r.center().y()), 4, 4);
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    p.setFont(f);
    p.setPen(t.ink);
    p.drawText(r.adjusted(24, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}
