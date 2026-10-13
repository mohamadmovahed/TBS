#pragma once

#include <tbsdvb/Scan.h>

#include <QString>
#include <QWidget>
#include <vector>

// IQ scatter plot of the demodulator's constellation samples (square, I right, Q up).
// Older samples fade out so the cloud stays readable while it updates.
class ConstellationView : public QWidget
{
    Q_OBJECT

public:
    explicit ConstellationView(QWidget *parent = nullptr);

    void setSamples(const std::vector<tbs::ConstellationSample> &samples);
    void setMessage(const QString &text); // shown instead of the plot (empty = plot)
    void clear();

    QSize sizeHint() const override { return {360, 360}; }
    QSize minimumSizeHint() const override { return {200, 200}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    std::vector<tbs::ConstellationSample> m_current;
    std::vector<tbs::ConstellationSample> m_previous;
    QString m_message;
    double m_scale = 0.0; // smoothed full-scale value
};
