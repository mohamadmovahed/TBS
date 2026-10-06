#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
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
    const QCommandLineOption autostart(QStringLiteral("autostart"),
                                       QStringLiteral("Start capturing with the last used settings"));
    parser.addOption(autostart);
    parser.process(app);

    MainWindow w;
    w.show();
    if (parser.isSet(autostart))
        QTimer::singleShot(0, &w, &MainWindow::startCapture);
    return app.exec();
}
