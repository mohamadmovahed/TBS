#pragma once

#include <QMainWindow>
#include <QVector>

class QTabWidget;
class ReceiverPage;

// Top-level window: one tab per receiver, so both tuners of a TBS 6903x (or any number of
// cards) capture, stream and preview at the same time, each with its own settings.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ReceiverPage *currentPage() const;
    QVector<ReceiverPage *> pages() const;

public slots:
    void startAll();
    ReceiverPage *addReceiver();
    // Forwarded to the current page (command line options)
    void showTab(const QString &name);
    void showSidebar(const QString &name);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    ReceiverPage *createPage(int index);
    void removePage(int tab);
    void renamePage(int tab);
    void updateTab(ReceiverPage *page);
    void toggleTheme();
    void saveLayout();

    QTabWidget *m_tabs = nullptr;
};
