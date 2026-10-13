#pragma once

#include <tbsbbproc/Processor.h>

#include <QSet>
#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Transport stream content: services with their components and now/next events,
// networks (NIT), bouquets (BAT), CA, and per-table statistics.
class ServicesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ServicesPanel(QWidget *parent = nullptr);

    void updateSnapshot(const tbs::bb::ProcessorSnapshot &snap);
    void reset();

signals:
    // Double-click on a service (audioPid -1) or on one of its audio streams
    void watchRequested(int isi, int programNumber, int audioPid);

private:
    void rebuildTree(const tbs::bb::TsReport &r);
    void updateTables(const tbs::bb::TsReport &r);
    QTreeWidgetItem *addItem(QTreeWidgetItem *parent, const QString &key, const QStringList &columns);

    QComboBox *m_stream = nullptr;
    QLabel *m_summary = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTableWidget *m_tables = nullptr;
    std::vector<int> m_streamIsis;
    size_t m_signature = 0;
    QSet<QString> m_expanded;
    QSet<QString> m_known; // keys seen before (new top-level groups start expanded)
};
