#pragma once

#include <tbsdvb/Status.h>

#include <QWidget>

class QGridLayout;
class QLabel;
class QFrame;

// Key/value cards with everything the receiver reports.
class OverviewPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPanel(QWidget *parent = nullptr);

    void updateFrontend(const tbs::FrontendStatus &st);
    void updateStats(const tbs::StreamStats &st);
    void setBbFrameMode(bool on);
    void reset();

private:
    QLabel *addRow(QGridLayout *grid, const QString &key);
    QFrame *makeCard(const QString &title, QGridLayout *&grid);

    QLabel *m_delsys = nullptr;
    QLabel *m_modcod = nullptr;
    QLabel *m_rolloff = nullptr;
    QLabel *m_freq = nullptr;
    QLabel *m_sr = nullptr;
    QLabel *m_streamId = nullptr;
    QLabel *m_ucb = nullptr;
    QLabel *m_temp = nullptr;

    QLabel *m_bytes = nullptr;
    QLabel *m_packets = nullptr;
    QLabel *m_cc = nullptr;
    QLabel *m_tei = nullptr;
    QLabel *m_sync = nullptr;
    QLabel *m_overflows = nullptr;

    QFrame *m_bbCard = nullptr;
    QLabel *m_bbFrames = nullptr;
    QLabel *m_bbRate = nullptr;
    QLabel *m_bbErrors = nullptr;
    QLabel *m_bbType = nullptr;
    QLabel *m_bbFlags = nullptr;
    QLabel *m_bbLengths = nullptr;

    QLabel *m_file = nullptr;
    QLabel *m_zmq = nullptr;
};
