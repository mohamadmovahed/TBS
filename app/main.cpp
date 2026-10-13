#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TbsReceiver"));
    QApplication::setApplicationName(QStringLiteral("TbsReceiver"));
    QApplication::setApplicationVersion(QStringLiteral(TBS_APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Tune a TBS 6903x and capture TS / BBFrames to file and ZeroMQ"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption autostart(QStringLiteral("autostart"), QStringLiteral("Start all receivers with the last used settings"));
    const QCommandLineOption theme(QStringLiteral("theme"), QStringLiteral("dark or light"), QStringLiteral("theme"));
    const QCommandLineOption tab(QStringLiteral("tab"), QStringLiteral("Initial details tab: overview, services, epg, monitor, ip, pids, log"),
                                 QStringLiteral("name"));
    const QCommandLineOption sidebar(QStringLiteral("sidebar"), QStringLiteral("Initial sidebar page: tuning or outputs"),
                                     QStringLiteral("page"));
    parser.addOption(sidebar);
    parser.addOption(autostart);
    parser.addOption(tab);
    parser.addOption(theme);
    parser.process(app);

    QString themeName = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    if (parser.isSet(theme))
        themeName = parser.value(theme);
    Theme::apply(app, themeName == QLatin1String("light") ? Theme::Mode::Light : Theme::Mode::Dark);

    MainWindow w;
    w.resize(1480, 940);
    w.show();
    if (parser.isSet(tab))
        w.showTab(parser.value(tab));
    if (parser.isSet(sidebar))
        w.showSidebar(parser.value(sidebar));
    if (parser.isSet(autostart))
        QTimer::singleShot(0, &w, &MainWindow::startAll);
    return app.exec();
}
