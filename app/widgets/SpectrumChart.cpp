#include "SpectrumChart.h"

#include "../Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

double niceStep(double range, int target)
{
    const double raw = range / std::max(1, target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * mag >= raw)
            return m * mag;
    return 10.0 * mag;
}

} // namespace

SpectrumChart::SpectrumChart(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    m_placeholder = tr("No spectrum yet");
}

void SpectrumChart::setSpectrum(const QVector<Point> &points, bool decibel)
{
    m_points = points;
    m_decibel = decibel;
    if (!m_points.isEmpty()) {
        m_fmin = m_points.front().frequencyMHz;
        m_fmax = std::max(m_points.back().frequencyMHz, m_fmin + 1.0);
        double lo = m_points.front().level, hi = lo;
        for (const Point &p : m_points) {
            lo = std::min(lo, p.level);
            hi = std::max(hi, p.level);
        }
        if (hi - lo < 1.0)
            hi = lo + 1.0;
        m_step = niceStep(hi - lo, 5);
        m_lo = std::floor(lo / m_step) * m_step;
        m_hi = std::ceil(hi / m_step) * m_step;
    }
    update();
}

void SpectrumChart::setMarkers(const QVector<Marker> &markers)
{
    m_markers = markers;
    m_selected = -1;
    update();
}

void SpectrumChart::setSelected(int marker)
{
    m_selected = marker;
    update();
}

void SpectrumChart::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    update();
}

void SpectrumChart::clear()
{
    m_points.clear();
    m_markers.clear();
    m_selected = -1;
    update();
}

QRectF SpectrumChart::plotRect() const
{
    const QFontMetrics fm(font());
    const qreal left = fm.horizontalAdvance(QStringLiteral("-100")) + 14;
    const qreal top = 18; // room for the carrier markers
    return QRectF(left, top, width() - left - 14, height() - top - fm.height() - 12);
}

double SpectrumChart::xOf(double mhz) const
{
    const QRectF r = plotRect();
    return r.left() + (mhz - m_fmin) / (m_fmax - m_fmin) * r.width();
}

double SpectrumChart::mhzAt(double x) const
{
    const QRectF r = plotRect();
    return m_fmin + (x - r.left()) / r.width() * (m_fmax - m_fmin);
}

int SpectrumChart::markerAt(double x) const
{
    int best = -1;
    double bestDist = 8.0; // px
    for (int i = 0; i < m_markers.size(); ++i) {
        const double d = std::fabs(xOf(m_markers[i].frequencyMHz) - x);
        const double half = m_markers[i].widthMHz > 0 ? std::fabs(xOf(m_markers[i].frequencyMHz + m_markers[i].widthMHz / 2)
                                                                  - xOf(m_markers[i].frequencyMHz))
                                                      : 0.0;
        const double dist = std::max(0.0, d - half);
        if (dist < bestDist || (dist == 0.0 && best >= 0 && d < std::fabs(xOf(m_markers[best].frequencyMHz) - x))) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void SpectrumChart::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm(font());
    if (m_points.size() < 2) {
        p.setPen(t.muted);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }
    const QRectF r = plotRect();
    auto yOf = [&](double v) { return r.bottom() - (v - m_lo) / (m_hi - m_lo) * r.height(); };

    // Grid and level axis
    p.setPen(QPen(t.hairline, 1));
    for (double v = m_lo; v <= m_hi + m_step / 2; v += m_step) {
        const qreal y = std::round(yOf(v)) + 0.5;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.setPen(t.muted);
    for (double v = m_lo; v <= m_hi + m_step / 2; v += m_step)
        p.drawText(QRectF(0, yOf(v) - fm.height() / 2.0, r.left() - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', m_step >= 1 ? 0 : 1));
    // Unit in the top left corner of the plot
    p.drawText(QRectF(r.left() + 6, r.top() + 2, 60, fm.height()), Qt::AlignLeft | Qt::AlignTop,
               m_decibel ? QStringLiteral("dB") : QStringLiteral("%"));

    // Frequency axis: about one label per 90 px
    const double fstep = niceStep(m_fmax - m_fmin, std::max(2, static_cast<int>(r.width() / 90)));
    for (double f = std::ceil(m_fmin / fstep) * fstep; f <= m_fmax; f += fstep) {
        const qreal x = xOf(f);
        p.setPen(QPen(t.hairline, 1));
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        p.setPen(t.muted);
        p.drawText(QRectF(x - 45, r.bottom() + 5, 90, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(f, 'f', fstep >= 1 ? 0 : 1) + (f + fstep > m_fmax ? QStringLiteral(" MHz") : QString()));
    }

    // Occupied bandwidth of the selected carrier behind the trace
    if (m_selected >= 0 && m_selected < m_markers.size() && m_markers[m_selected].widthMHz > 0) {
        const Marker &m = m_markers[m_selected];
        QColor band = t.accent;
        band.setAlphaF(0.12);
        const qreal x0 = std::max(r.left(), xOf(m.frequencyMHz - m.widthMHz / 2));
        const qreal x1 = std::min(r.right(), xOf(m.frequencyMHz + m.widthMHz / 2));
        p.fillRect(QRectF(x0, r.top(), std::max<qreal>(2, x1 - x0), r.height()), band);
    }

    // Trace: min/max per pixel column keeps narrow carriers visible at any width
    const int cols = std::max(1, static_cast<int>(r.width()));
    QVector<double> colMax(cols, std::nan("")), colMin(cols, std::nan(""));
    for (const Point &pt : m_points) {
        const int c = std::clamp(static_cast<int>((pt.frequencyMHz - m_fmin) / (m_fmax - m_fmin) * (cols - 1)), 0, cols - 1);
        colMax[c] = std::isnan(colMax[c]) ? pt.level : std::max(colMax[c], pt.level);
        colMin[c] = std::isnan(colMin[c]) ? pt.level : std::min(colMin[c], pt.level);
    }
    QPainterPath line;
    bool started = false;
    for (int c = 0; c < cols; ++c) {
        if (std::isnan(colMax[c]))
            continue;
        const qreal x = r.left() + c;
        if (!started) {
            line.moveTo(x, yOf(colMax[c]));
            started = true;
        } else {
            line.lineTo(x, yOf(colMax[c]));
        }
        if (colMin[c] < colMax[c] - 0.01) {
            line.lineTo(x, yOf(colMin[c]));
            line.lineTo(x, yOf(colMax[c]));
        }
    }
    QPainterPath fill = line;
    fill.lineTo(r.right(), r.bottom());
    fill.lineTo(r.left(), r.bottom());
    fill.closeSubpath();
    QColor area = t.accent;
    area.setAlphaF(0.10);
    p.fillPath(fill, area);
    p.setPen(QPen(t.accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);

    // Carrier markers above the plot
    for (int i = 0; i < m_markers.size(); ++i) {
        const Marker &m = m_markers[i];
        if (m.frequencyMHz < m_fmin || m.frequencyMHz > m_fmax)
            continue;
        const qreal x = xOf(m.frequencyMHz);
        const bool sel = i == m_selected;
        QPainterPath tri;
        const qreal s = sel ? 6.5 : 5.0;
        tri.moveTo(x - s, r.top() - 3 - s * 1.4);
        tri.lineTo(x + s, r.top() - 3 - s * 1.4);
        tri.lineTo(x, r.top() - 3);
        tri.closeSubpath();
        p.setPen(QPen(m.locked || sel ? t.accent : t.ink2, 1.2));
        p.setBrush(m.locked || sel ? QBrush(t.accent) : QBrush(t.surface));
        p.drawPath(tri);
    }

    // Hover: crosshair, value and the carrier under the cursor
    if (m_hoverX >= r.left() && m_hoverX <= r.right()) {
        const double f = mhzAt(m_hoverX);
        // nearest point
        auto it = std::lower_bound(m_points.begin(), m_points.end(), f,
                                   [](const Point &a, double v) { return a.frequencyMHz < v; });
        if (it == m_points.end())
            --it;
        if (it != m_points.begin() && std::fabs((it - 1)->frequencyMHz - f) < std::fabs(it->frequencyMHz - f))
            --it;
        const qreal x = xOf(it->frequencyMHz);
        p.setPen(QPen(t.muted, 1, Qt::DashLine));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(QPen(t.surface, 2));
        p.setBrush(t.accent);
        p.drawEllipse(QPointF(x, yOf(it->level)), 4.0, 4.0);

        QString value = QStringLiteral("%1 MHz  ·  %2 %3")
                            .arg(it->frequencyMHz, 0, 'f', 2)
                            .arg(it->level, 0, 'f', 1)
                            .arg(m_decibel ? QStringLiteral("dB") : QStringLiteral("%"));
        const int mi = markerAt(m_hoverX);
        const QString second = mi >= 0 ? m_markers[mi].label : QString();
        QFont bold = font();
        bold.setBold(true);
        const QFontMetrics bfm(bold);
        const qreal tw = std::max(bfm.horizontalAdvance(value), fm.horizontalAdvance(second)) + 20;
        const qreal th = bfm.height() + (second.isEmpty() ? 0 : fm.height() + 2) + 12;
        QRectF box(x + 12, r.top() + 6, tw, th);
        if (box.right() > width() - 4)
            box.moveRight(x - 12);
        p.setPen(QPen(t.hairline, 1));
        p.setBrush(t.raised);
        p.drawRoundedRect(box, 6, 6);
        p.setFont(bold);
        p.setPen(t.ink);
        p.drawText(box.adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop, value);
        if (!second.isEmpty()) {
            p.setFont(font());
            p.setPen(t.ink2);
            p.drawText(box.adjusted(10, 8 + bfm.height(), -10, 0), Qt::AlignLeft | Qt::AlignTop, second);
        }
    }
}

void SpectrumChart::mouseMoveEvent(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_hoverX = e->position().x();
#else
    m_hoverX = e->localPos().x();
#endif
    setCursor(markerAt(m_hoverX) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void SpectrumChart::mousePressEvent(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int i = markerAt(e->position().x());
#else
    const int i = markerAt(e->localPos().x());
#endif
    if (i >= 0) {
        m_selected = i;
        update();
        emit markerClicked(i);
    }
}

void SpectrumChart::mouseDoubleClickEvent(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int i = markerAt(e->position().x());
#else
    const int i = markerAt(e->localPos().x());
#endif
    if (i >= 0)
        emit markerActivated(i);
}

void SpectrumChart::leaveEvent(QEvent *)
{
    m_hoverX = -1;
    update();
}
