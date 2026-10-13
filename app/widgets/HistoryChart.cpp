#include "HistoryChart.h"

#include "../Theme.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

// 1-2-5 step giving about `target` intervals
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

HistoryChart::HistoryChart(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void HistoryChart::setSeries(const QVector<Point> &points, Kind kind, const QString &unit, int decimals)
{
    m_points = points;
    m_kind = kind;
    m_unit = unit;
    m_decimals = decimals;
    rebuild();
    update();
}

void HistoryChart::setBands(const QVector<Band> &bands)
{
    m_bands = bands;
    update();
}

void HistoryChart::setRange(qint64 fromMs, qint64 toMs)
{
    m_from = fromMs;
    m_to = std::max(toMs, fromMs + 1000);
    rebuild();
    update();
}

QRectF HistoryChart::plotRect() const
{
    const QFontMetrics fm(font());
    const qreal left = fm.horizontalAdvance(QStringLiteral("-000.0")) + 12;
    return QRectF(left, 10, width() - left - 12, height() - 10 - fm.height() - 10);
}

void HistoryChart::rebuild()
{
    m_buckets.clear();
    m_hover = -1;
    if (m_to <= m_from)
        return;
    // Bars at least 4 px wide (with a 2 px gap), lines at about 2 px per point
    const qreal w = std::max<qreal>(50, plotRect().width());
    const int count = std::max(1, static_cast<int>(w / (m_kind == Kind::Bars ? 6.0 : 2.0)));
    const qint64 span = m_to - m_from;
    const qint64 step = std::max<qint64>(1000, (span + count - 1) / count);
    const int n = static_cast<int>((span + step - 1) / step);
    m_buckets.resize(n);
    QVector<int> samples(n, 0);
    for (int i = 0; i < n; ++i)
        m_buckets[i] = {m_from + i * step, m_from + (i + 1) * step, std::nan("")};
    for (const Point &p : m_points) {
        if (p.timeMs < m_from || p.timeMs >= m_to || std::isnan(p.value))
            continue;
        const int i = static_cast<int>((p.timeMs - m_from) / step);
        Bucket &b = m_buckets[i];
        b.value = std::isnan(b.value) ? p.value : b.value + p.value;
        ++samples[i];
    }
    if (m_kind == Kind::Line)
        for (int i = 0; i < n; ++i)
            if (samples[i] > 1)
                m_buckets[i].value /= samples[i];
}

void HistoryChart::resizeEvent(QResizeEvent *)
{
    rebuild(); // the bucket width depends on the plot width
}

void HistoryChart::paintEvent(QPaintEvent *)
{
    const Theme::Tokens &t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    const QFontMetrics fm(font());

    double lo = 0, hi = 0;
    bool any = false;
    for (const Bucket &b : m_buckets) {
        if (std::isnan(b.value))
            continue;
        lo = any ? std::min(lo, b.value) : b.value;
        hi = any ? std::max(hi, b.value) : b.value;
        any = true;
    }
    if (!any) {
        p.setPen(t.muted);
        p.drawText(rect(), Qt::AlignCenter, tr("No data in this time range"));
        return;
    }
    if (m_kind == Kind::Bars || lo > 0)
        lo = std::min(0.0, lo); // counts and rates start at zero
    if (hi - lo < 1e-9)
        hi = lo + 1.0;
    const double step = niceStep(hi - lo, 4);
    lo = std::floor(lo / step) * step;
    hi = std::ceil(hi / step) * step;
    auto yOf = [&](double v) { return r.bottom() - (v - lo) / (hi - lo) * r.height(); };
    auto xOf = [&](qint64 ms) { return r.left() + double(ms - m_from) / double(m_to - m_from) * r.width(); };

    // Alarm periods behind the data, labelled with text ink
    QColor band = t.critical;
    band.setAlphaF(0.13);
    for (const Band &b : m_bands) {
        const qreal x0 = std::max(r.left(), xOf(b.fromMs));
        const qreal x1 = std::min(r.right(), xOf(b.toMs));
        if (x1 <= x0)
            continue;
        p.fillRect(QRectF(x0, r.top(), std::max<qreal>(2, x1 - x0), r.height()), band);
        if (x1 - x0 > fm.horizontalAdvance(b.label) + 8) {
            p.setPen(t.ink2);
            p.drawText(QRectF(x0 + 4, r.top() + 2, x1 - x0 - 8, fm.height()), Qt::AlignLeft | Qt::AlignTop, b.label);
        }
    }

    // Recessive grid and y labels
    p.setPen(QPen(t.hairline, 1));
    for (double v = lo; v <= hi + step / 2; v += step) {
        const qreal y = std::round(yOf(v)) + 0.5;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.setPen(t.muted);
    const int yDecimals = step >= 1 ? 0 : step >= 0.1 ? 1 : 2;
    for (double v = lo; v <= hi + step / 2; v += step)
        p.drawText(QRectF(0, yOf(v) - fm.height() / 2.0, r.left() - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', yDecimals));

    // Time labels: about one per 110 px
    const qint64 span = m_to - m_from;
    static const qint64 steps[] = {10000, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000, 7200000, 10800000, 21600000};
    qint64 tstep = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (qint64 s : steps)
        if (span / s <= std::max<qint64>(2, static_cast<qint64>(r.width() / 110))) {
            tstep = s;
            break;
        }
    const QString fmt = tstep < 60000 ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm");
    for (qint64 tm = (m_from / tstep + 1) * tstep; tm < m_to; tm += tstep) {
        const qreal x = xOf(tm);
        p.setPen(QPen(t.hairline, 1));
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        p.setPen(t.muted);
        p.drawText(QRectF(x - 40, r.bottom() + 5, 80, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                   QDateTime::fromMSecsSinceEpoch(tm).toString(fmt));
    }

    // Data
    if (m_kind == Kind::Line) {
        QPainterPath path;
        bool open = false;
        for (const Bucket &b : m_buckets) {
            if (std::isnan(b.value)) {
                open = false;
                continue;
            }
            const QPointF pt(xOf((b.fromMs + b.toMs) / 2), yOf(b.value));
            if (open)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            open = true;
        }
        p.setPen(QPen(t.accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent);
        for (const Bucket &b : m_buckets) {
            if (std::isnan(b.value) || b.value <= 0)
                continue;
            const qreal x0 = xOf(b.fromMs) + 1;
            const qreal x1 = xOf(b.toMs) - 1; // 2 px gap between bars
            const qreal y = yOf(b.value);
            QPainterPath bar;
            const qreal rad = std::min<qreal>(2, (x1 - x0) / 2);
            bar.addRoundedRect(QRectF(x0, y, std::max<qreal>(1, x1 - x0), r.bottom() - y + rad), rad, rad);
            p.save();
            p.setClipRect(QRectF(r.left(), r.top(), r.width(), r.bottom() - r.top()));
            p.drawPath(bar);
            p.restore();
        }
    }

    // Hover: crosshair, marker and tooltip
    if (m_hover >= 0 && m_hover < m_buckets.size()) {
        const Bucket &b = m_buckets[m_hover];
        const qreal x = xOf((b.fromMs + b.toMs) / 2);
        p.setPen(QPen(t.muted, 1, Qt::DashLine));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        QString value = std::isnan(b.value) ? tr("no data") : QString::number(b.value, 'f', m_decimals) + QLatin1Char(' ') + m_unit;
        if (!std::isnan(b.value) && m_kind == Kind::Line) {
            p.setPen(QPen(t.surface, 2));
            p.setBrush(t.accent);
            p.drawEllipse(QPointF(x, yOf(b.value)), 4.5, 4.5);
        }
        const QDateTime a = QDateTime::fromMSecsSinceEpoch(b.fromMs);
        const QDateTime z = QDateTime::fromMSecsSinceEpoch(b.toMs);
        QString when = b.toMs - b.fromMs <= 1000 ? a.toString(QStringLiteral("HH:mm:ss"))
                                                 : a.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("–")
                                                       + z.toString(QStringLiteral("HH:mm:ss"));
        for (const Band &band : m_bands)
            if (band.fromMs < b.toMs && band.toMs > b.fromMs)
                when += QStringLiteral("  ·  ") + band.label;
        QFont bold = font();
        bold.setBold(true);
        const QFontMetrics bfm(bold);
        const qreal tw = std::max(bfm.horizontalAdvance(value), fm.horizontalAdvance(when)) + 20;
        const qreal th = bfm.height() + fm.height() + 14;
        QRectF box(x + 12, r.top() + 4, tw, th);
        if (box.right() > width() - 4)
            box.moveRight(x - 12);
        p.setPen(QPen(t.hairline, 1));
        p.setBrush(t.raised);
        p.drawRoundedRect(box, 6, 6);
        p.setFont(bold);
        p.setPen(t.ink);
        p.drawText(box.adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop, value);
        p.setFont(font());
        p.setPen(t.ink2);
        p.drawText(box.adjusted(10, 6 + bfm.height() + 2, -10, 0), Qt::AlignLeft | Qt::AlignTop, when);
    }
}

void HistoryChart::mouseMoveEvent(QMouseEvent *e)
{
    const QRectF r = plotRect();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const qreal mx = e->position().x();
#else
    const qreal mx = e->localPos().x();
#endif
    int idx = -1;
    if (!m_buckets.isEmpty() && mx >= r.left() - 6 && mx <= r.right() + 6) {
        const double f = (mx - r.left()) / r.width();
        idx = std::clamp(static_cast<int>(f * m_buckets.size()), 0, static_cast<int>(m_buckets.size()) - 1);
    }
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
}

void HistoryChart::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
