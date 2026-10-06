#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

// Stat tile: caption, big value with unit, optional status chip and a
// sparkline of the recent history with a hover read-out.
class MetricTile : public QWidget
{
    Q_OBJECT

public:
    enum class Status { None, Good, Warning, Critical };

    explicit MetricTile(const QString &caption, QWidget *parent = nullptr);

    void setValue(const QString &value, const QString &unit = QString());
    void setSubtitle(const QString &text);
    void setStatus(Status status, const QString &label);
    // Appends a sample to the sparkline (NaN = gap)
    void addSample(double v);
    void setSparklineVisible(bool on);
    void setValueFormat(int decimals, const QString &unit);
    void clearHistory();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotRect() const;

    QString m_caption;
    QString m_value = QStringLiteral("–");
    QString m_unit;
    QString m_subtitle;
    Status m_status = Status::None;
    QString m_statusLabel;
    QVector<double> m_history;
    int m_capacity = 120;
    bool m_sparkline = true;
    int m_decimals = 1;
    QString m_sampleUnit;
    int m_hover = -1;
};
