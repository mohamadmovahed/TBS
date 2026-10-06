#pragma once

#include <QStringList>
#include <QWidget>

// Acquisition steps of the demodulator (Signal > Carrier > FEC > Sync > Lock)
// drawn as connected nodes; reached steps are filled.
class LockChain : public QWidget
{
    Q_OBJECT

public:
    explicit LockChain(QWidget *parent = nullptr);
    void setFlags(uint32_t flags);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    uint32_t m_flags = 0;
    QStringList m_names;
};
