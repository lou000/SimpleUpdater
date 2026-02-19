#ifndef CLIPARSER_H
#define CLIPARSER_H

#include <QDir>
#include <QStringList>
#include <QVersionNumber>
#include <QString>
#include <optional>

struct GenerateConfig {
    QDir directory;
    QString appExe;
    std::optional<QVersionNumber> minVersion;
};

struct UpdateConfig {
    QString source;
    QDir targetDir;
    bool forceUpdate;
    bool continueUpdate;
};

struct InstallConfig {
    std::optional<QDir> sourceDir;
    std::optional<QDir> targetDir;
};

enum class AppMode { Generate, Update, Install };

struct CliResult {
    AppMode mode;
    std::optional<GenerateConfig> generate;
    std::optional<UpdateConfig> update;
    std::optional<InstallConfig> install;
};

std::optional<CliResult> parseCli(const QCoreApplication& app);
std::optional<CliResult> parseCli(const QStringList& args);

bool isUrl(const QString& value);

#endif // CLIPARSER_H
