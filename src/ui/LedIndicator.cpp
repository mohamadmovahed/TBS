#include "LedIndicator.h"

#include <QPainter>

LedIndicator::LedIndicator(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void LedIndicator::setOn(bool on)
{
    if (m_on == on)
        return;
    m_on = on;
    update();
}

QSize LedIndicator::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.height() + 6 + fm.horizontalAdvance(m_text) + 4, fm.height() + 6);
}

void LedIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int d = fontMetrics().height() - 2;
    const QRect led(2, (height() - d) / 2, d, d);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(m_on ? m_onColor : palette().color(QPalette::Button).darker(130));
    p.drawEllipse(led);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(led.right() + 6, 0, width() - led.right() - 6, height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}
