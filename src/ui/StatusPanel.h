#pragma once

#include "core/FrontendStatus.h"
#include "core/StreamStats.h"

#include <QWidget>

class LedIndicator;
class QGridLayout;
class QGroupBox;
class QLabel;
class QProgressBar;

class StatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);

public slots:
    void updateFrontend(const FrontendStatus &st);
    void updateStats(const StreamStats &st);
    void reset();
    void setBbFrameMode(bool on);

private:
    QLabel *addValue(QGridLayout *grid, const QString &name);

    LedIndicator *m_ledSignal = nullptr;
    LedIndicator *m_ledCarrier = nullptr;
    LedIndicator *m_ledFec = nullptr;
    LedIndicator *m_ledSync = nullptr;
    LedIndicator *m_ledLock = nullptr;
    QProgressBar *m_signalBar = nullptr;
    QProgressBar *m_cnrBar = nullptr;

    QLabel *m_ber = nullptr;
    QLabel *m_ucb = nullptr;
    QLabel *m_temp = nullptr;
    QLabel *m_delsys = nullptr;
    QLabel *m_modcod = nullptr;
    QLabel *m_rolloff = nullptr;
    QLabel *m_freq = nullptr;
    QLabel *m_sr = nullptr;
    QLabel *m_streamId = nullptr;

    QLabel *m_rate = nullptr;
    QLabel *m_bytes = nullptr;
    QLabel *m_packets = nullptr;
    QLabel *m_ccErrors = nullptr;
    QLabel *m_tei = nullptr;
    QLabel *m_syncLoss = nullptr;
    QLabel *m_overflows = nullptr;

    QGroupBox *m_bbGroup = nullptr;
    QLabel *m_bbFrames = nullptr;
    QLabel *m_bbRate = nullptr;
    QLabel *m_bbErrors = nullptr;
    QLabel *m_bbMatype = nullptr;
    QLabel *m_bbLengths = nullptr;

    QLabel *m_file = nullptr;
    QLabel *m_zmq = nullptr;
};
