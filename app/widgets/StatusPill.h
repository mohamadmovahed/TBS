#pragma once

#include <QWidget>

// Rounded state indicator: coloured dot + text label on a tinted background.
class StatusPill : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { Neutral, Good, Warning, Critical, Accent };

    explicit StatusPill(QWidget *parent = nullptr);
    void setState(Kind kind, const QString &text);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Kind m_kind = Kind::Neutral;
    QString m_text;
};
