#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleHints>
#include <QVersionNumber>
#include "filehandler.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationVersion("0.1");
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    QCommandLineParser parser;
    parser.setApplicationDescription("SimpleUpdater");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption sourceLocation(QStringList()<<"s"<<"source",
                                  "Source location of the update.",
                                  "path/to/source");
    parser.addOption(sourceLocation);

    QCommandLineOption targetLocation(QStringList()<<"t"<<"target",
                                      "Target location of the update. If empty then target is application directory.",
                                      "path/to/source");
    parser.addOption(targetLocation);

    QCommandLineOption forceFullUpdate(QStringList()<<"f"<<"full",
                                  "Forces complete update even if only couple files changed.");
    parser.addOption(forceFullUpdate);

    QCommandLineOption applicationExe(QStringList()<<"e"<<"exe",
                                       "Original application executable, if not set the updater will not relaunch the original application.",
                                       "path/to/exe");
    parser.addOption(applicationExe);

    QCommandLineOption generateInfo(QStringList()<<"g"<<"generate",
                                      "Generate updateInfo.ini with settings and hashes for all the files in source directory recursively."
                                      "If source is not set application will use current directory."
                                      "Should be used inside the update directory.");
    parser.addOption(generateInfo);

    QCommandLineOption version(QStringList()<<"u"<<"update_version",
                                    "Sets a version of the update, to be used with -g/-generate, does nothing by itself",
                                    "d.d.d");
    parser.addOption(version);
    parser.process(a);

    QDir sourceDir;
    if(parser.isSet(sourceLocation))
    {
        QString sourcePath = parser.value(sourceLocation);
        sourceDir = QDir(sourcePath);
        if(!sourceDir.exists() || !sourceDir.isReadable())
        {
            qCritical()<<"Source location"<<sourcePath<<"is either invalid or not accesible!";
            return 1;
        }
    }

    QDir targetDir;
    if(parser.isSet(targetLocation))
    {
        QString targetPath = parser.value(targetLocation);
        targetDir = QDir(targetPath);
        if(!targetDir.exists() || !targetDir.isReadable())
        {
            qCritical()<<"Target location"<<targetPath<<"is either invalid or not accesible!";
            return 1;
        }
    }
    else
        targetDir = QApplication::applicationDirPath();

    QFile* originalApp = nullptr;
    if(parser.isSet(applicationExe))
    {
        QString appPath = parser.value(applicationExe);
        originalApp = new QFile(appPath);
        if(originalApp->exists() || originalApp->permissions() & QFile::ExeUser)
        {
            qCritical()<<"Error: Original application executable is not reachable or launching it is not permitted.";
            parser.showHelp(1);
        }
    }

    if(parser.isSet(generateInfo))
    {
        QDir dir = QApplication::applicationDirPath();
        if(parser.isSet(sourceLocation))
            dir = sourceDir;

        QVersionNumber v;
        if(parser.isSet(version))
        {
            auto str = parser.value(version);
            v = QVersionNumber::fromString(str);
            if(v.isNull() || v.segmentCount() == 0)
            {
                qDebug() << "Parsing failed for version string:" << str;
                return 1;
            }
        }
        qDebug()<<"Generating updateInfo.ini for";
        qDebug()<<"Hashing files...";
        FileHandler::generateInfoFile(dir, v);
        return 0;
    }

    // If installation is true then the application will proceed to ask for target filepath and then recursively copy
    // all files from current application directory to that location
    bool installation = parser.optionNames().isEmpty();
    MainWindow w(sourceDir, targetDir, originalApp, parser.isSet(forceFullUpdate), installation);
    w.show();
    return a.exec();
}
