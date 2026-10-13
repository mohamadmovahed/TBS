#pragma once

#include <tbsdvb/Types.h>

#include <QDialog>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

// A service the user can pick for a network output (from the live table analysis)
struct ServiceChoice
{
    int programNumber = 0;
    int isi = -1; // input stream carrying it (-1 = single stream / TS capture)
    QString name;
};

// Edits one TS network output (UDP, RTP or SRT; whole TS, PID list or one service).
class NetOutputDialog : public QDialog
{
    Q_OBJECT

public:
    NetOutputDialog(const tbs::TsNetworkOutput &output, const QVector<ServiceChoice> &services, bool bbMode,
                    QWidget *parent = nullptr);

    tbs::TsNetworkOutput output() const;

private:
    void updateVisibility();
    void updateUrl();
    bool collect(tbs::TsNetworkOutput *out, QString *error) const;

    QComboBox *m_protocol = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLabel *m_hostLabel = nullptr;
    QWidget *m_udpBox = nullptr;
    QSpinBox *m_ttl = nullptr;
    QLineEdit *m_iface = nullptr;
    QWidget *m_srtBox = nullptr;
    QComboBox *m_srtMode = nullptr;
    QSpinBox *m_latency = nullptr;
    QLineEdit *m_passphrase = nullptr;
    QLineEdit *m_streamId = nullptr;
    QComboBox *m_service = nullptr;
    QLineEdit *m_pids = nullptr;
    QSpinBox *m_isi = nullptr;
    QLabel *m_isiLabel = nullptr;
    QCheckBox *m_keepNull = nullptr;
    QSpinBox *m_perDatagram = nullptr;
    QLabel *m_url = nullptr;
    QLabel *m_error = nullptr;
    bool m_bbMode = false;
};
