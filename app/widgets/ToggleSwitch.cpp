#include "ToggleSwitch.h"

#include "../Theme.h"

#include <QPainter>

ToggleSwitch::ToggleSwitch(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
    const int textW = text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text()) + 10;
    return QSize(38 + textW, 24);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(1, (height() - 20) / 2.0, 36, 20);
    QColor trackColor = isChecked() ? t.accent : t.hairline;
    if (!isEnabled())
        trackColor.setAlphaF(0.4);
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, 10, 10);
    const double x = isChecked() ? track.right() - 10 : track.left() + 10;
    p.setBrush(QColor(Qt::white));
    p.drawEllipse(QPointF(x, track.center().y()), 7.5, 7.5);
    if (!text().isEmpty()) {
        p.setPen(isEnabled() ? t.ink : t.muted);
        p.drawText(QRectF(track.right() + 10, 0, width() - track.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}
