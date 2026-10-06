#pragma once

#include <tbsdvb/Status.h>

#include <QTableWidget>

// PID list with packet counts and an in-cell share bar.
class PidTable : public QTableWidget
{
    Q_OBJECT

public:
    explicit PidTable(QWidget *parent = nullptr);
    void updateStats(const tbs::StreamStats &st);
    void reset();
};
