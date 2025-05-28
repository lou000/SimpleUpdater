#include "filehandler.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QSettings>
#include <QThread>

FileHandler::FileHandler(QObject *parent)
    : QObject(parent)
{

}

bool FileHandler::_copyDirectoryRecursively(const QDir& source, const QDir& target, QSet<QString>* visited)
{
    auto self = QFileInfo(QApplication::applicationFilePath());

    if(cancelRequested.load())
    {
        if(!canceled.load())
        {
            emit cancelled();
            canceled.store(true);
        }
        return false;
    }

    if(!source.exists())
    {
        qWarning()<<"Source directory does not exist:"<<source.absolutePath();
        visited->insert(source.absolutePath());
        return false;
    }

    QString srcPath = source.absolutePath();
    QString tgtPath = target.absolutePath();

    if(srcPath == tgtPath)
    {
        qWarning()<<"Source and target directories are the same.";
        visited->insert(srcPath);
        return false;
    }

    if(visited->contains(srcPath))
    {
        qWarning()<<"Detected loop or repeated directory:"<<srcPath;
        visited->insert(srcPath);
        return false;
    }

    QDir tgt = target;
    if(!tgt.exists() && !tgt.mkpath("."))
    {
        qWarning()<<"Failed to create target directory:"<<tgtPath;
        visited->insert(srcPath);
        return false;
    }

    QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);

    bool success = true;
    for (const QFileInfo& entry : entries)
    {
        QString srcFilePath = entry.absoluteFilePath();
        QString tgtFilePath = tgt.filePath(entry.fileName());

        if(entry.isSymLink())
        {
            QString linkTarget = QFileInfo(srcFilePath).readSymLink();
            if(!QFile::link(linkTarget, tgtFilePath))
            {
                qWarning()<<"Failed to create symbolic link:"<<tgtFilePath;
                success = false;
                emit progressUpdated({tgtFilePath+" (COPY) - failed to create symlink", false});
            }
            else
                emit progressUpdated({tgtFilePath+" (COPY)", true});
        }
        else if(entry.isDir())
        {
            if(!_copyDirectoryRecursively(QDir(srcFilePath), QDir(tgtFilePath), visited))
                success = false;
        }
        else if(entry.isFile())
        {
            if(QFile::exists(tgtFilePath))
            {
                if(QFileInfo(tgtFilePath) == self)
                    continue;

                QFile::remove(tgtFilePath);
            }
            if(!QFile::copy(srcFilePath, tgtFilePath))
            {
                qWarning()<<"Failed to copy"<<srcFilePath<<"to"<<tgtFilePath;
                success = false;
            }
            visited->insert(srcFilePath);
            emit progressUpdated({tgtFilePath+" (COPY)", success});
        }
        else
        {
            success = false;
            qWarning()<<"Unknown entry type skipped:"<<srcFilePath;
            emit progressUpdated({tgtFilePath+" (COPY) - ???", false});
        }

        if(cancelRequested.load())
        {
            if(!canceled.load())
            {
                emit cancelled();
                canceled.store(true);
            }
            return false;
        }
    }

    visited->insert(srcPath);
    return success;
}

bool FileHandler::copyDirectoryRecursively(QDir source, QDir target)
{
    QSet<QString> visited;
    bool success = _copyDirectoryRecursively(source, target, &visited);
    cancelRequested.store(false);
    canceled.store(false);
    return success;
}

bool FileHandler::copyFiles(QDir source, QDir target, QStringList filePaths, bool cancelable)
{
    bool updateSuccess = true;
    auto self = QFileInfo(QApplication::applicationFilePath());

    for(const auto& relPath : filePaths)
    {
        if (cancelRequested.load() && cancelable)
        {
            updateSuccess = false;
            break;
        }

        QString srcPath = source.filePath(relPath);
        QString tgtPath = target.filePath(relPath);
        if(QFileInfo(tgtPath) == self)
        {
            emit progressUpdated({tgtPath+" (SKIP)", true});
            continue;
        }

        QFileInfo srcInfo(srcPath);
        if (!srcInfo.exists())
        {
            qWarning() << "File does not exist:" << srcPath;
            emit progressUpdated({tgtPath+" (COPY) - file does not exist", false});
            continue;
        }

        QDir tgtDir = QFileInfo(tgtPath).absoluteDir();
        if (!tgtDir.exists() && !tgtDir.mkpath("."))
        {
            qWarning() << "Failed to create target subdirectory for" << tgtPath;
            emit progressUpdated({tgtPath+" (COPY) - cannot create subdirectory", false});
            continue;
        }
        if(QFile::exists(tgtPath))
        {
            if(QFileInfo(tgtPath) == self)
                continue;
            QFile::remove(tgtPath);
        }
        bool success = QFile::copy(srcPath, tgtPath);
        emit progressUpdated({tgtPath+" (COPY)", success});
        if(!success)
            updateSuccess = false;
    }
    cancelRequested.store(false);
    return updateSuccess;
}

bool FileHandler::removeFiles(QDir dir, QStringList filePaths)
{
    bool allRemoved = true;
    auto self = QFileInfo(QApplication::applicationFilePath());

    for (const auto& relPath : filePaths)
    {
        QString filePath = dir.filePath(relPath);
        if(QFileInfo(filePath) == self)
            continue;

        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists())
        {
            qWarning() << "File does not exist:" << filePath;
            emit progressUpdated({filePath + " (REMOVE)", false});
            continue;
        }

        bool success = QFile::remove(filePath);
        emit progressUpdated({filePath + " (REMOVE)", success});
        if (!success)
        {
            qWarning() << "Failed to remove file:" << filePath;
            allRemoved = false;
        }
    }
    return allRemoved;
}


int FileHandler::getFileCountRecursive(const QDir &source)
{
    int files = 0;
    std::function<void(const QDir&)> collectFiles = [&](const QDir& dir) {
        QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries) {
            if (entry.isDir())
                collectFiles(QDir(entry.absoluteFilePath()));
            else if (entry.isFile())
                files++;
        }
    };
    collectFiles(source);
    return files;
}

QByteArray FileHandler::hashFile(QFile &file)
{
    if (!file.open(QFile::ReadOnly))
    {
        qWarning()<<"Failed to open"<<file.fileName()<<"for reading!\n"<<file.errorString();
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

void FileHandler::generateInfoFile(const QDir& directory, const QVersionNumber &version,
                                   const QString& appExe, bool full, bool force)
{
    //Remove old file if it exists
    auto updateInfo = QSettings(directory.absoluteFilePath("updateInfo.ini"), QSettings::IniFormat);
    QStringList groups = updateInfo.childGroups();

    updateInfo.beginGroup("SETTINGS");
    updateInfo.setValue("app_version", version.toString());
    if(!appExe.isEmpty())
        updateInfo.setValue("app_exe", appExe);
    updateInfo.setValue("full_update", full);
    updateInfo.setValue("force_update", force);
    updateInfo.setValue("file_count", 0);
    updateInfo.endGroup();

    if(groups.contains("FILE_HASHES"))
        updateInfo.remove("FILE_HASHES");

    int fileCount = 0;
    std::function<void(QDir)> hashFileRecursive = [&](QDir dir){
        QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries)
        {
            if (entry.isDir())
                hashFileRecursive(QDir(entry.absoluteFilePath()));
            else if (entry.isFile())
            {
                auto filePath = entry.absoluteFilePath();
                auto file = QFile(filePath);
                QByteArray hash = hashFile(file);

                //encoding filepath to base64 and ovoid / clashing in QSettings
                filePath = directory.relativeFilePath(filePath);
                updateInfo.setValue(filePath.replace('/', "|"), hash);
                fileCount++;
            }
        }
    };

    updateInfo.beginGroup("FILE_HASHES");
    hashFileRecursive(directory);
    updateInfo.endGroup();
    updateInfo.setValue("SETTINGS/file_count", fileCount);
}
