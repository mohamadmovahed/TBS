#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

// Time series chart for the Monitor tab: one metric (no second axis), drawn as a line
// (levels, rates) or bars (error counts), with shaded alarm periods and a hover
// crosshair + tooltip. Values are bucketed so marks stay readable at any range.
class HistoryChart : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { Line, Bars };

    struct Point
    {
        qint64 timeMs;
        double value; // NaN = gap
    };
    struct Band
    {
        qint64 fromMs;
        qint64 toMs;
        QString label;
    };

    explicit HistoryChart(QWidget *parent = nullptr);

    void setSeries(const QVector<Point> &points, Kind kind, const QString &unit, int decimals);
    void setBands(const QVector<Band> &bands);
    void setRange(qint64 fromMs, qint64 toMs);

    QSize minimumSizeHint() const override { return {320, 180}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Bucket
    {
        qint64 fromMs;
        qint64 toMs;
        double value; // mean (line) or sum (bars); NaN = no data
    };
    void rebuild();
    QRectF plotRect() const;

    QVector<Point> m_points;
    QVector<Band> m_bands;
    QVector<Bucket> m_buckets;
    Kind m_kind = Kind::Line;
    QString m_unit;
    int m_decimals = 1;
    qint64 m_from = 0;
    qint64 m_to = 0;
    int m_hover = -1;
};
