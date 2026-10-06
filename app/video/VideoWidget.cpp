#include "VideoWidget.h"

#include "../Theme.h"

#include <QPainter>
#include <QPainterPath>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_message(tr("No video"))
{
    setMinimumSize(320, 180);
}

void VideoWidget::setFrame(const QImage &image)
{
    m_image = image;
    update();
}

void VideoWidget::setMessage(const QString &message)
{
    m_message = message;
    update();
}

void VideoWidget::setInfo(const QString &info)
{
    m_info = info;
    update();
}

void VideoWidget::clear()
{
    m_image = QImage();
    m_info.clear();
    update();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    p.fillPath(clip, QColor(8, 8, 8));
    p.setClipPath(clip);

    if (m_image.isNull()) {
        // Placeholder: muted play glyph + message
        const QPointF c = rect().center();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 18));
        p.drawEllipse(c - QPointF(0, 18), 26, 26);
        QPainterPath tri;
        tri.moveTo(c.x() - 8, c.y() - 30);
        tri.lineTo(c.x() + 12, c.y() - 18);
        tri.lineTo(c.x() - 8, c.y() - 6);
        tri.closeSubpath();
        p.setBrush(QColor(255, 255, 255, 90));
        p.drawPath(tri);
        p.setPen(QColor(255, 255, 255, 150));
        p.drawText(QRectF(rect()).adjusted(20, height() / 2.0 + 18, -20, 0), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   m_message);
    } else {
        QSize sz = m_image.size();
        sz.scale(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
        p.drawImage(target, m_image);
    }

    if (!m_info.isEmpty() && !m_image.isNull()) {
        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const int w = QFontMetrics(f).horizontalAdvance(m_info) + 20;
        const QRectF chip(12, 12, w, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(chip, 12, 12);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, m_info);
    }
    p.setClipping(false);
    p.setPen(QPen(t.hairline, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(clip);
}
