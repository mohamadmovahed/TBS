#include "MetricTile.h"

#include "../Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <limits>

MetricTile::MetricTile(const QString &caption, QWidget *parent)
    : QWidget(parent)
    , m_caption(caption.toUpper())
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize MetricTile::sizeHint() const
{
    return QSize(220, 132);
}

QSize MetricTile::minimumSizeHint() const
{
    return QSize(150, 132);
}

void MetricTile::setValue(const QString &value, const QString &unit)
{
    m_value = value;
    m_unit = unit;
    update();
}

void MetricTile::setSubtitle(const QString &text)
{
    m_subtitle = text;
    update();
}

void MetricTile::setStatus(Status status, const QString &label)
{
    m_status = status;
    m_statusLabel = label;
    update();
}

void MetricTile::addSample(double v)
{
    m_history.append(v);
    if (m_history.size() > m_capacity)
        m_history.remove(0, m_history.size() - m_capacity);
    update();
}

void MetricTile::setSparklineVisible(bool on)
{
    m_sparkline = on;
    update();
}

void MetricTile::setValueFormat(int decimals, const QString &unit)
{
    m_decimals = decimals;
    m_sampleUnit = unit;
}

void MetricTile::clearHistory()
{
    m_history.clear();
    m_hover = -1;
    update();
}

QRectF MetricTile::plotRect() const
{
    return QRectF(14, height() - 44, width() - 28, 32);
}

void MetricTile::mouseMoveEvent(QMouseEvent *event)
{
    const QRectF pr = plotRect().adjusted(0, -8, 0, 8);
    int hover = -1;
    if (m_sparkline && m_history.size() > 1 && pr.contains(event->pos())) {
        const double step = pr.width() / (m_capacity - 1);
        const int offset = m_capacity - m_history.size();
        const int idx = static_cast<int>(std::lround((event->pos().x() - pr.left()) / step)) - offset;
        if (idx >= 0 && idx < m_history.size())
            hover = idx;
    }
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void MetricTile::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void MetricTile::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Card
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(t.hairline, 1));
    p.setBrush(t.surface);
    p.drawRoundedRect(card, 12, 12);

    // Caption
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(f);
    p.setPen(t.muted);
    p.drawText(QRectF(14, 12, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, m_caption);

    // Status chip (dot + label, never colour alone)
    if (m_status != Status::None) {
        const QColor c = m_status == Status::Good ? t.good : m_status == Status::Warning ? t.warning : t.critical;
        QFont sf = font();
        sf.setPixelSize(11);
        sf.setWeight(QFont::DemiBold);
        const QFontMetrics fm(sf);
        const int w = fm.horizontalAdvance(m_statusLabel) + 24;
        const QRectF chip(width() - 14 - w, 10, w, 20);
        QColor bg = c;
        bg.setAlphaF(0.16);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(chip, 10, 10);
        p.setBrush(c);
        p.drawEllipse(QPointF(chip.left() + 10, chip.center().y()), 3.5, 3.5);
        p.setFont(sf);
        p.setPen(t.ink);
        p.drawText(chip.adjusted(17, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, m_statusLabel);
    }

    // Value + unit
    QFont vf = font();
    vf.setPixelSize(28);
    vf.setWeight(QFont::DemiBold);
    p.setFont(vf);
    p.setPen(t.ink);
    const QFontMetrics vfm(vf);
    const int baseline = 34 + vfm.ascent();
    p.drawText(QPointF(14, baseline), m_value);
    if (!m_unit.isEmpty()) {
        QFont uf = font();
        uf.setPixelSize(13);
        p.setFont(uf);
        p.setPen(t.ink2);
        p.drawText(QPointF(14 + vfm.horizontalAdvance(m_value) + 6, baseline), m_unit);
    }
    if (!m_subtitle.isEmpty()) {
        QFont sf = font();
        sf.setPixelSize(11);
        p.setFont(sf);
        p.setPen(t.muted);
        p.drawText(QRectF(14, baseline + 4, width() - 28, 16), Qt::AlignLeft | Qt::AlignTop, m_subtitle);
    }

    if (!m_sparkline)
        return;

    // Sparkline: single series, 2px line, soft area fill, hover crosshair + read-out
    const QRectF pr = plotRect();
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (double v : m_history) {
        if (std::isnan(v))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    p.setPen(QPen(t.hairline, 1));
    p.drawLine(pr.bottomLeft(), pr.bottomRight());
    if (lo > hi || m_history.size() < 2)
        return;
    if (hi - lo < 1e-9) {
        hi += 0.5;
        lo -= 0.5;
    }
    const double pad = (hi - lo) * 0.1;
    lo -= pad;
    hi += pad;
    const double step = pr.width() / (m_capacity - 1);
    const int offset = m_capacity - m_history.size();
    auto pt = [&](int i) {
        const double x = pr.left() + (i + offset) * step;
        const double y = pr.bottom() - (m_history[i] - lo) / (hi - lo) * pr.height();
        return QPointF(x, y);
    };

    QPainterPath line;
    QPainterPath area;
    bool open = false;
    QPointF first;
    QPointF last;
    for (int i = 0; i < m_history.size(); ++i) {
        if (std::isnan(m_history[i])) {
            if (open) {
                area.lineTo(last.x(), pr.bottom());
                area.lineTo(first.x(), pr.bottom());
                area.closeSubpath();
            }
            open = false;
            continue;
        }
        const QPointF q = pt(i);
        if (!open) {
            line.moveTo(q);
            area.moveTo(q);
            first = q;
            open = true;
        } else {
            line.lineTo(q);
            area.lineTo(q);
        }
        last = q;
    }
    if (open) {
        area.lineTo(last.x(), pr.bottom());
        area.lineTo(first.x(), pr.bottom());
        area.closeSubpath();
    }
    QLinearGradient grad(pr.topLeft(), pr.bottomLeft());
    QColor a = t.accent;
    a.setAlphaF(0.28);
    grad.setColorAt(0, a);
    a.setAlphaF(0.0);
    grad.setColorAt(1, a);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPath(area);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(line);

    if (m_hover >= 0 && m_hover < m_history.size() && !std::isnan(m_history[m_hover])) {
        const QPointF q = pt(m_hover);
        p.setPen(QPen(t.muted, 1, Qt::DashLine));
        p.drawLine(QPointF(q.x(), pr.top() - 4), QPointF(q.x(), pr.bottom()));
        p.setPen(QPen(t.surface, 2));
        p.setBrush(t.accent);
        p.drawEllipse(q, 4.5, 4.5);

        const int ago = m_history.size() - 1 - m_hover;
        const QString text = QStringLiteral("%1 %2 · %3")
                                 .arg(m_history[m_hover], 0, 'f', m_decimals)
                                 .arg(m_sampleUnit)
                                 .arg(ago == 0 ? tr("now") : tr("%1 s ago").arg(ago * m_sampleSeconds, 0, 'f', m_sampleSeconds < 1.0 ? 1 : 0));
        QFont tf = font();
        tf.setPixelSize(11);
        p.setFont(tf);
        const QFontMetrics tfm(tf);
        const int tw = tfm.horizontalAdvance(text) + 14;
        QRectF tip(q.x() - tw / 2.0, pr.top() - 30, tw, 20);
        tip.moveLeft(std::clamp(tip.left(), 4.0, width() - tw - 4.0));
        p.setPen(QPen(t.hairline, 1));
        p.setBrush(t.raised);
        p.drawRoundedRect(tip, 6, 6);
        p.setPen(t.ink);
        p.drawText(tip, Qt::AlignCenter, text);
    }
}
