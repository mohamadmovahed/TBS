#include "VideoWidget.h"

#include <QPainter>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_message(tr("No video"))
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
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

void VideoWidget::clear()
{
    m_image = QImage();
    update();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_image.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    QSize sz = m_image.size();
    sz.scale(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, m_image);
}
