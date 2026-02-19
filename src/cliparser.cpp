#include "cliparser.h"
#include "platform/platform.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

bool isUrl(const QString& value)
{
    return value.startsWith("http://", Qt::CaseInsensitive)
        || value.startsWith("https://", Qt::CaseInsensitive);
}

static std::optional<CliResult> parseGenerate(const QStringList& args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Generate a manifest for the application directory.");

    QCommandLineOption appExeOpt(QStringList() << "app_exe",
                                 "Relative path to the application executable.",
                                 "path/to/exe");
    parser.addOption(appExeOpt);

    QCommandLineOption minVersionOpt(QStringList() << "min_version",
                                     "Minimum version required for this update (forces update if target is older).",
                                     "d.d.d");
    parser.addOption(minVersionOpt);

    parser.addHelpOption();
    parser.addPositionalArgument("directory", "Directory to generate the manifest for.", "[directory]");

    parser.process(args);

    if(!parser.isSet(appExeOpt))
    {
        qCritical().noquote() << "--app_exe is required for the 'generate' subcommand.";
        return std::nullopt;
    }

    QDir directory;
    auto positional = parser.positionalArguments();
    if(!positional.isEmpty())
        directory = QDir(positional.first());
    else
        directory = QDir::current();

    if(!directory.exists())
    {
        qCritical().noquote() << "Directory does not exist:" << directory.absolutePath();
        return std::nullopt;
    }

    QString appExe = parser.value(appExeOpt);

    if(!directory.exists(appExe))
    {
        qCritical().noquote() << "Application executable not found:" << directory.absoluteFilePath(appExe);
        return std::nullopt;
    }

    auto version = Platform::readExeVersion(directory.absoluteFilePath(appExe));
    if(!version)
    {
        qCritical().noquote() << "Cannot read version information from:" << directory.absoluteFilePath(appExe)
                              << "\nThe executable must have embedded version resources (VERSIONINFO on Windows).";
        return std::nullopt;
    }

    std::optional<QVersionNumber> minVersion;
    if(parser.isSet(minVersionOpt))
    {
        auto str = parser.value(minVersionOpt);
        auto v = QVersionNumber::fromString(str);
        if(v.isNull() || v.segmentCount() == 0)
        {
            qCritical().noquote() << "Invalid --min_version value:" << str;
            return std::nullopt;
        }
        minVersion = v;
    }

    GenerateConfig gen;
    gen.directory = directory;
    gen.appExe = appExe;
    gen.minVersion = minVersion;

    CliResult result;
    result.mode = AppMode::Generate;
    result.generate = gen;
    return result;
}

static std::optional<CliResult> parseUpdate(const QStringList& args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Update the target application from a source.");

    QCommandLineOption sourceOpt(QStringList() << "s" << "source",
                                 "Source location (local path, UNC path, or URL).",
                                 "path/or/url");
    parser.addOption(sourceOpt);

    QCommandLineOption targetOpt(QStringList() << "t" << "target",
                                 "Target directory to update. Defaults to the updater's own directory.",
                                 "path/to/target");
    parser.addOption(targetOpt);

    QCommandLineOption forceOpt(QStringList() << "force",
                                "Force the update (user cannot skip).");
    parser.addOption(forceOpt);

    QCommandLineOption continueOpt(QStringList() << "continue-update",
                                   "Continue a self-update in progress (internal use).");
    parser.addOption(continueOpt);

    parser.addHelpOption();
    parser.process(args);

    if(!parser.isSet(sourceOpt))
    {
        qCritical().noquote() << "--source is required for the 'update' subcommand.";
        return std::nullopt;
    }

    QString sourceValue = parser.value(sourceOpt);

    if(!isUrl(sourceValue))
    {
        QDir srcDir(sourceValue);
        if(!srcDir.exists())
        {
            qCritical().noquote() << "Source directory does not exist or is not accessible:" << sourceValue;
            return std::nullopt;
        }
    }

    QDir targetDir;
    if(parser.isSet(targetOpt))
    {
        targetDir = QDir(parser.value(targetOpt));
        if(!targetDir.exists())
        {
            qCritical().noquote() << "Target directory does not exist or is not accessible:" << parser.value(targetOpt);
            return std::nullopt;
        }
    }
    else
    {
        targetDir = QDir(QApplication::applicationDirPath());
    }

    UpdateConfig upd;
    upd.source = sourceValue;
    upd.targetDir = targetDir;
    upd.forceUpdate = parser.isSet(forceOpt);
    upd.continueUpdate = parser.isSet(continueOpt);

    CliResult result;
    result.mode = AppMode::Update;
    result.update = upd;
    return result;
}

static std::optional<CliResult> parseInstall(const QStringList& args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Install the application to a target directory.");

    QCommandLineOption sourceOpt(QStringList() << "s" << "source",
                                 "Source directory containing the application files.",
                                 "path/to/source");
    parser.addOption(sourceOpt);

    QCommandLineOption targetOpt(QStringList() << "t" << "target",
                                 "Target directory where the application will be installed.",
                                 "path/to/target");
    parser.addOption(targetOpt);

    parser.addHelpOption();
    parser.process(args);

    InstallConfig inst;

    if(parser.isSet(sourceOpt))
    {
        QDir srcDir(parser.value(sourceOpt));
        if(!srcDir.exists())
        {
            qCritical().noquote() << "Source directory does not exist or is not accessible:" << parser.value(sourceOpt);
            return std::nullopt;
        }
        inst.sourceDir = srcDir;
    }

    if(parser.isSet(targetOpt))
        inst.targetDir = QDir(parser.value(targetOpt));

    CliResult result;
    result.mode = AppMode::Install;
    result.install = inst;
    return result;
}

std::optional<CliResult> parseCli(const QCoreApplication& app)
{
    return parseCli(app.arguments());
}

std::optional<CliResult> parseCli(const QStringList& args)
{
    if(args.isEmpty())
    {
        qCritical().noquote() << "No arguments provided (expected at least program name).";
        return std::nullopt;
    }

    if(args.size() < 2)
    {
        InstallConfig inst;
        inst.sourceDir = QDir(QApplication::applicationDirPath());
        CliResult result;
        result.mode = AppMode::Install;
        result.install = inst;
        return result;
    }

    QString subcommand = args.at(1);

    QStringList subArgs;
    subArgs.append(args.first());
    for(int i = 2; i < args.size(); ++i)
        subArgs.append(args.at(i));

    if(subcommand == "generate")
        return parseGenerate(subArgs);

    if(subcommand == "update")
        return parseUpdate(subArgs);

    if(subcommand == "install")
        return parseInstall(subArgs);

    // Legacy flag compat: old app launches updater with "-u -s <path>"
    if(subcommand == "-u" || subcommand == "--update")
    {
        qWarning().noquote() << "Legacy flag" << subcommand
                             << "detected, treating as 'update' subcommand.";
        return parseUpdate(subArgs);
    }

    if(subcommand.startsWith('-'))
    {
        QCommandLineParser parser;
        parser.setApplicationDescription("SimpleUpdater");
        parser.addHelpOption();
        parser.addVersionOption();
        parser.process(args);
    }

    qCritical().noquote() << "Unknown command:" << subcommand
                          << "\nRun with no arguments or --help for usage information.";
    return std::nullopt;
}
