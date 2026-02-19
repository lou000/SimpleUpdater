#include "manifest.h"
#include "platform/platform.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>


static QByteArray hashFile(const QString& filePath)
{
    QFile file(filePath);
    if(!file.open(QFile::ReadOnly))
    {
        qWarning() << "Failed to open" << filePath << "for hashing:" << file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();
    return hash.result();
}

static bool shouldSkipFile(const QString& fileName)
{
    return fileName == "manifest.json"
        || fileName == "manifest.json.tmp"
        || fileName == "updateInfo.ini";
}

std::optional<Manifest> readManifest(const QString& jsonPath)
{
    QFile file(jsonPath);
    if(!file.open(QFile::ReadOnly))
    {
        qWarning() << "Cannot open manifest:" << jsonPath << file.errorString();
        return std::nullopt;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if(parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "Invalid JSON in" << jsonPath << ":" << parseError.errorString();
        return std::nullopt;
    }

    if(!doc.isObject())
    {
        qWarning() << "Manifest root is not a JSON object:" << jsonPath;
        return std::nullopt;
    }

    QJsonObject root = doc.object();

    if(!root.contains("version") || !root["version"].isString())
    {
        qWarning() << "Manifest missing or invalid 'version' field:" << jsonPath;
        return std::nullopt;
    }
    auto version = QVersionNumber::fromString(root["version"].toString());
    if(version.isNull())
    {
        qWarning() << "Cannot parse version string:" << root["version"].toString() << "in" << jsonPath;
        return std::nullopt;
    }

    if(!root.contains("app_exe") || !root["app_exe"].isString())
    {
        qWarning() << "Manifest missing or invalid 'app_exe' field:" << jsonPath;
        return std::nullopt;
    }

    if(!root.contains("files") || !root["files"].isObject())
    {
        qWarning() << "Manifest missing or invalid 'files' field:" << jsonPath;
        return std::nullopt;
    }

    Manifest manifest;
    manifest.version = version;
    manifest.appExe = root["app_exe"].toString();

    if(root.contains("min_version") && root["min_version"].isString())
    {
        auto mv = QVersionNumber::fromString(root["min_version"].toString());
        if(!mv.isNull())
        {
            if(QVersionNumber::compare(mv, version) > 0)
            {
                qWarning() << "min_version" << mv.toString()
                           << "is greater than version" << version.toString()
                           << "in" << jsonPath;
                return std::nullopt;
            }
            manifest.minVersion = mv;
        }
    }

    QJsonObject filesObj = root["files"].toObject();
    for(auto it = filesObj.constBegin(); it != filesObj.constEnd(); ++it)
    {
        if(!it.value().isString())
        {
            qWarning() << "Non-string hash for file" << it.key() << "in" << jsonPath;
            return std::nullopt;
        }
        manifest.files.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
    }

    return manifest;
}

bool writeManifest(const QString& jsonPath, const Manifest& manifest)
{
    QJsonObject root;
    root["version"] = manifest.version.toString();
    root["app_exe"] = manifest.appExe;

    if(manifest.minVersion)
        root["min_version"] = manifest.minVersion->toString();

    QJsonObject filesObj;
    for(auto it = manifest.files.constBegin(); it != manifest.files.constEnd(); ++it)
        filesObj.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
    root["files"] = filesObj;

    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

    QString tmpPath = jsonPath + ".tmp";
    QFile tmpFile(tmpPath);
    if(!tmpFile.open(QFile::WriteOnly | QFile::Truncate))
    {
        qWarning() << "Cannot write manifest tmp file:" << tmpPath << tmpFile.errorString();
        return false;
    }
    if(tmpFile.write(jsonData) != jsonData.size())
    {
        qWarning() << "Incomplete write to:" << tmpPath;
        tmpFile.close();
        QFile::remove(tmpPath);
        return false;
    }
    tmpFile.close();

    if(QFile::exists(jsonPath) && !QFile::remove(jsonPath))
    {
        qWarning() << "Cannot remove old manifest:" << jsonPath;
        QFile::remove(tmpPath);
        return false;
    }
    if(!QFile::rename(tmpPath, jsonPath))
    {
        qWarning() << "Cannot rename" << tmpPath << "to" << jsonPath;
        return false;
    }

    return true;
}

QHash<QString, QByteArray> hashDirectory(const QDir& directory)
{
    QHash<QString, QByteArray> files;

    QDirIterator it(directory.absolutePath(),
                    QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    while(it.hasNext())
    {
        it.next();
        QFileInfo info = it.fileInfo();

        if(info.isSymLink())
        {
            qWarning() << "Skipping symlink:" << info.absoluteFilePath();
            continue;
        }

        if(shouldSkipFile(info.fileName()))
            continue;

        QByteArray hash = hashFile(info.absoluteFilePath());
        if(hash.isEmpty())
            continue;

        QString relPath = directory.relativeFilePath(info.absoluteFilePath());
        files.insert(relPath, hash);
    }

    return files;
}

std::optional<Manifest> generateManifest(const QDir& directory, const QString& appExe,
                                         const std::optional<QVersionNumber>& minVersion)
{
    if(!directory.exists(appExe))
    {
        qCritical().noquote() << "Application executable not found:" << directory.absoluteFilePath(appExe);
        return std::nullopt;
    }

    auto version = Platform::readExeVersion(directory.absoluteFilePath(appExe));
    if(!version)
    {
        qCritical().noquote() << "Cannot read version from:" << directory.absoluteFilePath(appExe);
        return std::nullopt;
    }

    QString manifestPath = directory.absoluteFilePath("manifest.json");
    auto existing = readManifest(manifestPath);
    if(existing && existing->version == version.value())
    {
        qCritical().noquote()
            << "Version" << version->toString()
            << "matches the existing manifest. Bump the version before regenerating.";
        return std::nullopt;
    }

    QHash<QString, QByteArray> files;
    QDirIterator it(directory.absolutePath(),
                    QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    while(it.hasNext())
    {
        it.next();
        QFileInfo info = it.fileInfo();

        if(info.isSymLink())
        {
            qWarning() << "Skipping symlink:" << info.absoluteFilePath();
            continue;
        }

        if(shouldSkipFile(info.fileName()))
            continue;

        QByteArray hash = hashFile(info.absoluteFilePath());
        if(hash.isEmpty())
        {
            qCritical().noquote() << "Cannot read" << info.absoluteFilePath() << "- aborting generation";
            return std::nullopt;
        }

        QString relPath = directory.relativeFilePath(info.absoluteFilePath());
        files.insert(relPath, hash);
    }

    if(minVersion && QVersionNumber::compare(*minVersion, version.value()) > 0)
    {
        qCritical().noquote()
            << "min_version" << minVersion->toString()
            << "is greater than version" << version->toString();
        return std::nullopt;
    }

    Manifest manifest;
    manifest.version = version.value();
    manifest.minVersion = minVersion;
    manifest.appExe = appExe;
    manifest.files = files;

    if(!writeManifest(manifestPath, manifest))
    {
        qCritical().noquote() << "Failed to write manifest to:" << manifestPath;
        return std::nullopt;
    }

    return manifest;
}
