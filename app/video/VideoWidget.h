#pragma once

#include <QImage>
#include <QWidget>

// Video surface: rounded card, letterboxed frame, info chip and placeholder text.
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);

public slots:
    void setFrame(const QImage &image);
    void setMessage(const QString &message);
    void setInfo(const QString &info);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_message;
    QString m_info;
};
