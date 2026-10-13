#include "ConstellationView.h"

#include "../Theme.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

ConstellationView::ConstellationView(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ConstellationView::setSamples(const std::vector<tbs::ConstellationSample> &samples)
{
    m_previous.swap(m_current);
    m_current = samples;
    m_message.clear();
    // Full scale: 99th percentile of |I|, |Q|, smoothed so the plot does not jump
    if (!samples.empty()) {
        std::vector<int> mags;
        mags.reserve(samples.size());
        for (const auto &s : samples)
            mags.push_back(std::max(std::abs(int(s.i)), std::abs(int(s.q))));
        const size_t k = mags.size() * 99 / 100;
        std::nth_element(mags.begin(), mags.begin() + static_cast<std::ptrdiff_t>(k), mags.end());
        const double target = std::max(64, mags[k]) * 1.15;
        m_scale = m_scale <= 0 ? target : m_scale * 0.8 + target * 0.2;
    }
    update();
}

void ConstellationView::setMessage(const QString &text)
{
    m_message = text;
    update();
}

void ConstellationView::clear()
{
    m_current.clear();
    m_previous.clear();
    m_scale = 0;
    update();
}

void ConstellationView::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int side = std::min(width(), height()) - 8;
    const QRectF r((width() - side) / 2.0, (height() - side) / 2.0, side, side);

    p.setPen(QPen(t.hairline, 1));
    p.setBrush(t.surface);
    p.drawRoundedRect(r, 8, 8);
    if (!m_message.isEmpty() || (m_current.empty() && m_previous.empty())) {
        p.setPen(t.muted);
        p.drawText(r.adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                   m_message.isEmpty() ? tr("Waiting for samples") : m_message);
        return;
    }
    // Axes and the unit circle (rms of the outer ring for PSK)
    const QPointF c = r.center();
    p.setPen(QPen(t.hairline, 1));
    p.drawLine(QPointF(r.left() + 8, c.y()), QPointF(r.right() - 8, c.y()));
    p.drawLine(QPointF(c.x(), r.top() + 8), QPointF(c.x(), r.bottom() - 8));
    p.setPen(t.muted);
    p.drawText(QRectF(r.right() - 26, c.y() - 18, 20, 16), Qt::AlignRight, QStringLiteral("I"));
    p.drawText(QRectF(c.x() + 6, r.top() + 4, 20, 16), Qt::AlignLeft, QStringLiteral("Q"));

    const double half = side / 2.0 - 10;
    const double scale = m_scale > 0 ? half / m_scale : 1.0;
    p.setPen(Qt::NoPen);
    auto draw = [&](const std::vector<tbs::ConstellationSample> &v, double alpha) {
        QColor col = t.accent;
        col.setAlphaF(alpha);
        p.setBrush(col);
        for (const auto &s : v) {
            const QPointF pt(c.x() + s.i * scale, c.y() - s.q * scale);
            if (r.contains(pt))
                p.drawEllipse(pt, 1.6, 1.6);
        }
    };
    draw(m_previous, 0.18);
    draw(m_current, 0.55);
}
