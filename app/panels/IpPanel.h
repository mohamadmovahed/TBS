#pragma once

#include <tbsbbproc/Processor.h>

#include <QWidget>
#include <vector>

class MetricTile;
class QComboBox;
class QLabel;
class QStackedWidget;
class QTableWidget;

// GSE / IP view: rates, protocol and label shares, top flows, GSE health and the pcap output.
class IpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit IpPanel(QWidget *parent = nullptr);

    void updateSnapshot(const tbs::bb::ProcessorSnapshot &snap);
    void reset();

private:
    QComboBox *m_stream = nullptr;
    QLabel *m_gse = nullptr;
    QStackedWidget *m_stack = nullptr;
    MetricTile *m_rate = nullptr;
    MetricTile *m_pps = nullptr;
    MetricTile *m_pdus = nullptr;
    MetricTile *m_errors = nullptr;
    QTableWidget *m_protocols = nullptr;
    QTableWidget *m_labels = nullptr;
    QTableWidget *m_flows = nullptr;
    QLabel *m_pcap = nullptr;
    std::vector<int> m_isis;
    uint64_t m_lastErrors = 0;
};
