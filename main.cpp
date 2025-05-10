#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationVersion("0.1");
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    QCommandLineParser parser;
    parser.setApplicationDescription("SimpleUpdater");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption updateLocation(QStringList() << "l" << "location",
                                  "Location of a new application version.",
                                  "location");
    parser.addOption(updateLocation);

    QCommandLineOption forceFullUpdate(QStringList() << "f" << "full",
                                  "Forces complete update even if only couple files changed.",
                                  "full");
    parser.addOption(forceFullUpdate);

    QCommandLineOption applicationExe(QStringList() << "e" << "exe",
                                       "Original application executable, if not set the updater will not relaunch the original application.",
                                       "exe");
    parser.addOption(applicationExe);
    parser.process(a);

    if(!parser.isSet(updateLocation))
    {
        qCritical() << "Error: Location option is required.";
        parser.showHelp(1);
    }
    auto originalApp = new QFile();
    if(parser.isSet(updateLocation))
    {
        originalApp = new QFile(parser.value(applicationExe));
        if(originalApp->exists() || originalApp->permissions() & QFile::ExeUser)
        {
            qCritical() << "Error: Original application executable is not reachable or launching it is not permitted.";
            parser.showHelp(1);
        }
    }
    QString updatePath = parser.value(updateLocation);
    if(auto path = new QDir(updatePath); path->exists() && path->isReadable())
    {
        MainWindow w(path, originalApp, parser.isSet(forceFullUpdate));
        w.show();
        return a.exec();
    }
    else
    {
        qCritical() << "Error: Location is unreachable on unreadable.";
        parser.showHelp(1);
    }
}
