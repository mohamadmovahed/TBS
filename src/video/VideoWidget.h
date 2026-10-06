#pragma once

#include <QImage>
#include <QWidget>

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);

public slots:
    void setFrame(const QImage &image);
    void setMessage(const QString &message);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_message;
};
