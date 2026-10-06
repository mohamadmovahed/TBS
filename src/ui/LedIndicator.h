#pragma once

#include <QColor>
#include <QWidget>

class LedIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit LedIndicator(const QString &text, QWidget *parent = nullptr);

    void setOn(bool on);
    void setOnColor(const QColor &c) { m_onColor = c; update(); }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    bool m_on = false;
    QColor m_onColor = QColor(40, 200, 60);
};
