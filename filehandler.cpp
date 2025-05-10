#include "filehandler.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QSettings>

FileHandler::FileHandler(QObject *parent)
    : QObject(parent)
{
    connect(this, &FileHandler::cancelled, this, [this](){copyFinished(false);});
}

bool FileHandler::_copyDirectoryRecursively(const QDir& source, const QDir& target, QMap<QString, bool>* visited)
{
    auto exeInfo = QFileInfo(QApplication::applicationFilePath()); // to skip self

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
        visited->insert(source.absolutePath(), false);
        return false;
    }

    QString srcPath = source.absolutePath();
    QString tgtPath = target.absolutePath();

    if(srcPath == tgtPath)
    {
        qWarning()<<"Source and target directories are the same.";
        visited->insert(srcPath, false);
        return false;
    }

    if(visited->contains(srcPath))
    {
        qWarning()<<"Detected loop or repeated directory:"<<srcPath;
        visited->insert(srcPath, false);
        return false;
    }

    QDir tgt = target;
    if(!tgt.exists() && !tgt.mkpath("."))
    {
        qWarning()<<"Failed to create target directory:"<<tgtPath;
        visited->insert(srcPath, false);
        return false;
    }

    QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);

    for (const QFileInfo& entry : entries)
    {
        QString srcFilePath = entry.absoluteFilePath();
        QString tgtFilePath = tgt.filePath(entry.fileName());

        if(entry.isSymLink())
        {
            QString linkTarget = QFileInfo(srcFilePath).readSymLink();
            if(!QFile::link(linkTarget, tgtFilePath))
                qWarning()<<"Failed to create symbolic link:"<<tgtFilePath;
        }
        else if(entry.isDir())
            _copyDirectoryRecursively(QDir(srcFilePath), QDir(tgtFilePath), visited);
        else if(entry.isFile())
        {
            if(entry == exeInfo) // skip self
                continue;
            FileHandler::copyFileSafely(srcFilePath, tgtFilePath);
            visited->insert(srcFilePath, true);
            emit progressUpdated(*visited);
        }
        else
            qWarning()<<"Unknown file type skipped:"<<srcFilePath;
    }
    visited->insert(srcPath, true);
    return true;
}

void FileHandler::copyDirectoryRecursively(const QDir &source, const QDir &target)
{
    QMap<QString, bool> visited;
    bool success = _copyDirectoryRecursively(source, target, &visited);
    emit copyFinished(success);
}

int FileHandler::getNumberOfFilesRecursive(const QDir &source)
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
        qWarning()<<"Failed to open"<<file.fileName()<<"for reading!";
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

void FileHandler::generateInfoFile(const QDir& directory, const QVersionNumber& version)
{
    //Remove old file if it exists
    auto updateInfo = QSettings("updateInfo.ini", QSettings::IniFormat);
    QStringList groups = updateInfo.childGroups();

    if(!groups.contains("SETTINGS"))
    {
        updateInfo.beginGroup("SETTINGS");
        updateInfo.setValue("app_version", QString());
        updateInfo.setValue("force_update", false);
        updateInfo.setValue("full_update", false);
        updateInfo.setValue("new_file_count", 0);
        updateInfo.endGroup();
    }
    if(groups.contains("FILE_HASHES"))
        updateInfo.remove("FILE_HASHES");

    auto exeInfo = QFileInfo(QApplication::applicationFilePath());

    int fileCount = 0;
    std::function<void(QDir)> hashFileRecursive = [&](QDir dir){
        QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries)
        {
            if (entry.isDir())
                hashFileRecursive(QDir(entry.absoluteFilePath()));
            else if (entry.isFile())
            {
                if(entry == exeInfo) // skip self
                    continue;
                auto filePath = entry.absoluteFilePath();
                filePath = directory.relativeFilePath(filePath);
                qDebug()<<filePath;
                auto file = QFile(filePath);
                QByteArray hash = hashFile(file);
                //encoding filepath to base64 and ovoid / clashing in QSettings
                updateInfo.setValue(filePath.replace('/', "|"), hash);
                fileCount++;
            }
        }
    };

    updateInfo.beginGroup("FILE_HASHES");
    hashFileRecursive(directory);
    updateInfo.endGroup();
    updateInfo.setValue("SETTINGS/new_file_count", fileCount);

    if(!version.isNull() || version.segmentCount() == 0)
        updateInfo.setValue("SETTINGS/app_version", version.toString());
}

bool FileHandler::copyFileSafely(const QString &src, const QString &dst)
{
    QString backup = dst + ".bak";
    if (QFile::exists(dst)) {
        QFile::remove(backup);
        if (!QFile::rename(dst, backup))
        {
            qWarning()<<"Failed to backup existing file:"<<dst;
            return false;
        }
    }

    if (!QFile::copy(src, dst))
    {
        QFile::rename(backup, dst);
        qWarning()<<"Failed to copy"<<src<<"to"<<dst<<"restoring backup...";
        return false;
    }

    QFile::remove(backup);
    return true;
}
