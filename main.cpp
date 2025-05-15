#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QStyleHints>
#include <QVersionNumber>
#include "filehandler.h"
#include <QTextStream>

bool checkRequired(const QCommandLineParser& parser, const QCommandLineOption& dependent, const QCommandLineOption& required)
{
    if(parser.isSet(dependent) && !parser.isSet(required))
    {
        QMessageBox::critical(nullptr,"Required option missing!", "Error: --"+dependent.names().first()+" requires --"+required.names().first()+" to also be set.\n");
        exit(1);
        return false;
    }
    return true;
}

bool checkRequiredOneOf(const QCommandLineParser& parser, const QCommandLineOption& dependent, const QList<QCommandLineOption>& options)
{
    if(!parser.isSet(dependent))
        return true;

    for(const QCommandLineOption& opt : options)
        if(parser.isSet(opt))
            return true;

    QString err = "Error: At least one of the following options must be set with "+dependent.names().first()+" :";
    for(const QCommandLineOption& opt : options)
        err+="\n --"+opt.names().first();
    QMessageBox::critical(nullptr,"Required option missing!",err);
    exit(1);
    return false;
}

bool checkExclusive(const QCommandLineParser& parser, const QCommandLineOption& a, const QCommandLineOption& b)
{
    if(parser.isSet(a) && parser.isSet(b))
    {
        QMessageBox::critical(nullptr,"Required option missing!", "Error: --"+a.names().first()+" and --" +b.names().first()+" cannot be used together.\n");
        exit(1);
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationVersion("0.1");
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    QCommandLineParser parser;
    parser.setApplicationDescription("SimpleUpdater");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption updateMode(QStringList()<<"u"<<"update",
                                  "Sets the operating mode to update.");
    parser.addOption(updateMode);

    QCommandLineOption sourceLocation(QStringList()<<"s"<<"source",
                                      "Source location of the update. Required in update mode, in generate mode defaults to current directory.",
                                      "path/to/source");
    parser.addOption(sourceLocation);

    QCommandLineOption targetLocation(QStringList()<<"t"<<"target",
                                      "Target location of the update. If empty then target is current directory.",
                                      "path/to/source");
    parser.addOption(targetLocation);

    QCommandLineOption generateInfo(QStringList()<<"g"<<"generate",
                                      "Sets the operating mode to generate. "
                                      "Generates updateInfo.ini with settings and hashes for all the files in source directory recursively. "
                                      "If source is not set application will use current directory.");
    parser.addOption(generateInfo);

    QCommandLineOption version(QStringList()<<"app_version",
                                    "Sets a version of the update in updateInfo.ini file. Used only with -g/-generate. Does nothing by itself.",
                                    "d.d.d");
    parser.addOption(version);

    QCommandLineOption fullUpdate(QStringList()<<"full_update",
                                  "Sets a flag in updateInfo.ini file that forces complete update even if only couple files changed. "
                                  "Used only with -g/-generate. Does nothing by itself.");
    parser.addOption(fullUpdate);

    QCommandLineOption forceUpdate(QStringList()<<"force_update",
                                   "Sets a flag in updateInfo.ini file that forces the update. "
                                   "Normally user has a choice to postpone the update, this flag disables this choice.");
    parser.addOption(forceUpdate);

    QCommandLineOption applicationExe(QStringList()<<"app_exe",
                                      "Sets a path relative to source location of updated application executable in updateInfo.ini. "
                                      "If not set the updater/installer will not relaunch the original application. "
                                      "Used only with -g/-generate. Does nothing by itself.",
                                      "path/to/exe");
    parser.addOption(applicationExe);
    parser.process(a);

    // cant be in updateMode and generateInfo
    checkExclusive(parser, updateMode, generateInfo);

    //source location is required in update mode
    checkRequired(parser, updateMode, sourceLocation);

    // target location can only be used with updateMode
    checkRequired(parser, targetLocation, updateMode);

    // version is required when generating info
    checkRequired(parser, generateInfo, version);

    //options that require generateInfo to be set
    checkRequired(parser, version, generateInfo);
    checkRequired(parser, fullUpdate, generateInfo);
    checkRequired(parser, forceUpdate, generateInfo);
    checkRequired(parser, applicationExe, generateInfo);

    bool installation = !parser.isSet(updateMode) && !parser.isSet(updateMode);

    std::optional<QDir> sourceDir;
    if(parser.isSet(sourceLocation))
    {
        QString sourcePath = parser.value(sourceLocation);
        auto dir = QDir(sourcePath);
        if(dir.exists() && dir.isReadable())
            sourceDir = dir;
        else
        {
            qFatal()<<"Source location"<<sourcePath<<"is either invalid or not accesible!";
            return 1;
        }
    }

    std::optional<QDir> targetDir;
    if(parser.isSet(targetLocation))
    {
        QString targetPath = parser.value(targetLocation);
        auto dir = QDir(targetPath);
        if(dir.exists() && dir.isReadable())
            targetDir = dir;
        else
        {
            qFatal()<<"Target location"<<targetPath<<"is either invalid or not accesible!";
            return 1;
        }
    }

    if(parser.isSet(generateInfo))
    {
        QDir dir = QDir();
        if(sourceDir)
            dir = sourceDir.value();

        QVersionNumber v;
        if(parser.isSet(version))
        {
            auto str = parser.value(version);
            v = QVersionNumber::fromString(str);
            if(v.isNull() || v.segmentCount() == 0)
            {
                qFatal() << "Parsing failed for version string:" << str;
                return 1;
            }
        }

        QString appExe = "";
        if(parser.isSet(applicationExe))
        {
            appExe = parser.value(applicationExe);
            if(!dir.exists(appExe))
            {
                qFatal()<<"Error: Application executable"<<dir.absoluteFilePath(appExe)<<"set in --app_exe is not reachable.";
                return 1;
            }
        }

        bool full = parser.isSet(fullUpdate);
        bool force = parser.isSet(forceUpdate);

        FileHandler::generateInfoFile(dir, v, appExe, full, force);
        return 0;
    }

    MainWindow w(sourceDir, targetDir, installation);
    w.show();
    int ret = a.exec();

    return ret;
}
