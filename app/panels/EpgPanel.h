#pragma once

#include <tbsbbproc/PsiTypes.h>

#include <QMap>
#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTreeWidget;

// Electronic programme guide from the EIT (present/following and schedule):
// services on the left, their programmes by day on the right, details below.
class EpgPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EpgPanel(QWidget *parent = nullptr);

    // EPG of one transport stream (isi -1 = single stream / TS capture). streamUtc is the
    // stream's TDT time (0 = unknown, use the clock); it marks the programme running now.
    void setSchedules(int isi, const std::vector<tbs::bb::ServiceSchedule> &schedules, int64_t streamUtc);
    void reset();

signals:
    void watchRequested(int isi, int programNumber, int audioPid);

private:
    void rebuildStreams();
    void rebuildServices();
    void rebuildEvents();
    void showDetails();
    void exportXmltv();
    int currentIsi() const;
    const tbs::bb::ServiceSchedule *currentService() const;
    int64_t nowUtc() const;

    QComboBox *m_stream = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_export = nullptr;
    QLabel *m_summary = nullptr;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_services = nullptr;
    QTreeWidget *m_events = nullptr;
    QTextBrowser *m_details = nullptr;

    QMap<int, std::vector<tbs::bb::ServiceSchedule>> m_data; // by ISI
    QMap<int, int64_t> m_streamUtc;
    QMap<int, qint64> m_streamUtcAt; // local clock when m_streamUtc was received
    QString m_selectedKey;           // "onid.tsid.sid" of the service shown
    bool m_scrolledToNow = false;
};
