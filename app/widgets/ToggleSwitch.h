#pragma once

#include <QAbstractButton>

// iOS-style on/off switch with an optional text label on the right.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ToggleSwitch(const QString &text = QString(), QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
