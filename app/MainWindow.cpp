#include "MainWindow.h"

#include "ReceiverPage.h"
#include "Theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

namespace {

// Tab status dot (colour plus the tooltip text, never colour alone)
QIcon dotIcon(const QColor &c)
{
    QPixmap pm(QSize(12, 12) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(2, 2, 8, 8));
    return QIcon(pm);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TBS Receiver"));
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->tabBar()->setExpanding(false);
    auto *add = new QToolButton;
    add->setText(tr("+ Add receiver"));
    add->setObjectName(QStringLiteral("Ghost"));
    add->setToolTip(tr("Open another tuner (e.g. the second tuner of a TBS 6903x) in its own tab"));
    m_tabs->setCornerWidget(add, Qt::TopRightCorner);
    setCentralWidget(m_tabs);

    // Ctrl+Tab / Ctrl+PgDown and back switch receivers
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)})
        connect(new QShortcut(k, this), &QShortcut::activated, this,
                [this]() { m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count()); });
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageUp)})
        connect(new QShortcut(k, this), &QShortcut::activated, this, [this]() {
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
        });
    connect(add, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(addReceiver()); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::removePage);
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, &MainWindow::renamePage);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
        for (ReceiverPage *p : pages())
            p->setActive(p == currentPage());
    });

    QSettings s;
    createPage(0);
    // Indices of the extra receivers, e.g. "1, 2" (strings, so the file stays hand-editable)
    for (const QString &v : s.value(QStringLiteral("receivers/list")).toStringList()) {
        const int index = v.trimmed().toInt();
        if (index > 0 && index < 64)
            createPage(index);
    }
    m_tabs->setCurrentIndex(0);
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
}

ReceiverPage *MainWindow::currentPage() const
{
    return qobject_cast<ReceiverPage *>(m_tabs->currentWidget());
}

QVector<ReceiverPage *> MainWindow::pages() const
{
    QVector<ReceiverPage *> out;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *p = qobject_cast<ReceiverPage *>(m_tabs->widget(i)))
            out.push_back(p);
    return out;
}

ReceiverPage *MainWindow::createPage(int index)
{
    auto *page = new ReceiverPage(index);
    const int tab = m_tabs->addTab(page, page->name());
    if (index == 0) // the first receiver stays
        m_tabs->tabBar()->setTabButton(tab, QTabBar::RightSide, nullptr);
    connect(page, &ReceiverPage::themeToggleRequested, this, &MainWindow::toggleTheme);
    connect(page, &ReceiverPage::summaryChanged, this, [this, page]() { updateTab(page); });
    page->setActive(m_tabs->currentWidget() == page);
    updateTab(page);
    return page;
}

ReceiverPage *MainWindow::addReceiver()
{
    int next = 1;
    for (ReceiverPage *p : pages())
        next = std::max(next, p->index() + 1);
    ReceiverPage *page = createPage(next);
    saveLayout();
    return page;
}

void MainWindow::removePage(int tab)
{
    auto *page = qobject_cast<ReceiverPage *>(m_tabs->widget(tab));
    if (!page || page->index() == 0)
        return;
    if (page->isRunning()
        && QMessageBox::question(this, windowTitle(), tr("Stop %1 and close it?").arg(page->name()))
               != QMessageBox::Yes)
        return;
    page->shutdown();
    m_tabs->removeTab(tab);
    // Its settings go with it
    QSettings s;
    s.remove(QStringLiteral("receivers/%1").arg(page->index()));
    page->deleteLater();
    saveLayout();
}

void MainWindow::renamePage(int tab)
{
    auto *page = qobject_cast<ReceiverPage *>(m_tabs->widget(tab));
    if (!page)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename receiver"), tr("Name"), QLineEdit::Normal, page->name(), &ok);
    if (ok)
        page->setName(name);
}

void MainWindow::updateTab(ReceiverPage *page)
{
    const int tab = m_tabs->indexOf(page);
    if (tab < 0)
        return;
    const Theme::Tokens &t = Theme::tokens();
    QColor c = t.muted;
    QString state = tr("stopped");
    if (page->isRunning()) {
        c = page->isLocked() ? t.good : t.warning;
        state = page->isLocked() ? tr("locked") : tr("searching");
    }
    m_tabs->setTabText(tab, page->name());
    m_tabs->setTabIcon(tab, dotIcon(c));
    m_tabs->setTabToolTip(tab, tr("%1: %2 · double-click to rename").arg(page->name(), state));
}

void MainWindow::toggleTheme()
{
    Theme::apply(*qApp, Theme::mode() == Theme::Mode::Dark ? Theme::Mode::Light : Theme::Mode::Dark);
    for (ReceiverPage *p : pages()) {
        p->refreshTheme();
        updateTab(p);
    }
    QSettings().setValue(QStringLiteral("ui/theme"),
                         Theme::mode() == Theme::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
}

void MainWindow::saveLayout()
{
    QStringList list;
    for (ReceiverPage *p : pages())
        if (p->index() > 0)
            list << QString::number(p->index());
    QSettings s;
    s.setValue(QStringLiteral("receivers/list"), list);
}

void MainWindow::startAll()
{
    for (ReceiverPage *p : pages())
        p->startCapture();
}

void MainWindow::showTab(const QString &name)
{
    if (ReceiverPage *p = currentPage())
        p->showTab(name);
}

void MainWindow::showSidebar(const QString &name)
{
    if (ReceiverPage *p = currentPage())
        p->showSidebar(name);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (ReceiverPage *p : pages())
        p->shutdown();
    saveLayout();
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/theme"), Theme::mode() == Theme::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
    event->accept();
}
