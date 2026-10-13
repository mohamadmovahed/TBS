#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

// RF spectrum of one polarization: level over frequency as a line with a light fill, one
// marker per carrier (filled = locked by the blind scan, outlined = candidate), a hover
// crosshair with the frequency and level, and click to select a carrier.
class SpectrumChart : public QWidget
{
    Q_OBJECT

public:
    struct Point
    {
        double frequencyMHz;
        double level;
    };
    struct Marker
    {
        double frequencyMHz;
        double widthMHz; // occupied bandwidth (symbol rate x 1.2), 0 = unknown
        QString label;   // tooltip line, e.g. "11494.120 MHz · 27500 kS/s · DVB-S2 8PSK"
        bool locked;
    };

    explicit SpectrumChart(QWidget *parent = nullptr);

    void setSpectrum(const QVector<Point> &points, bool decibel);
    void setMarkers(const QVector<Marker> &markers);
    void setSelected(int marker);
    void setPlaceholder(const QString &text);
    void clear();

    QSize minimumSizeHint() const override { return {420, 220}; }

signals:
    void markerClicked(int index);
    void markerActivated(int index); // double click

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plotRect() const;
    double xOf(double mhz) const;
    double mhzAt(double x) const;
    int markerAt(double x) const;

    QVector<Point> m_points;
    QVector<Marker> m_markers;
    bool m_decibel = true;
    double m_fmin = 0, m_fmax = 1, m_lo = 0, m_hi = 1, m_step = 1;
    int m_selected = -1;
    double m_hoverX = -1;
    QString m_placeholder;
};
