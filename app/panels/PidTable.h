#pragma once

#include <tbsdvb/Status.h>

#include <QTableWidget>
#include <map>
#include <string>

// PID list with packet counts and an in-cell share bar.
class PidTable : public QTableWidget
{
    Q_OBJECT

public:
    explicit PidTable(QWidget *parent = nullptr);
    void updateStats(const tbs::StreamStats &st);
    void reset();
    // Descriptions from the PSI analysis (PMT, video, ECM, ...) override the built-in names
    void setPidUsage(const std::map<uint16_t, std::string> &usage);

private:
    std::map<uint16_t, std::string> m_usage;
};
