#include "filecopier.h"

FileCopier::FileCopier(QObject *parent) : QObject(parent)
{
    connect(this, &FileCopier::cancelled, this, [this](){copyFinished(false);});
}

bool FileCopier::_copyDirectoryRecursively(const QDir& source, const QDir& target, QMap<QString, bool>* visited)
{
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
        qWarning() << "Source directory does not exist:" << source.absolutePath();
        visited->insert(source.absolutePath(), false);
        return false;
    }

    QString srcPath = source.absolutePath();
    QString tgtPath = target.absolutePath();

    if(srcPath == tgtPath)
    {
        qWarning() << "Source and target directories are the same.";
        visited->insert(srcPath, false);
        return false;
    }

    if(visited->contains(srcPath))
    {
        qWarning() << "Detected loop or repeated directory:" << srcPath;
        visited->insert(srcPath, false);
        return false;
    }

    QDir tgt = target;
    if(!tgt.exists() && !tgt.mkpath("."))
    {
        qWarning() << "Failed to create target directory:" << tgtPath;
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
                qWarning() << "Failed to create symbolic link:" << tgtFilePath;
        }
        else if(entry.isDir())
            _copyDirectoryRecursively(QDir(srcFilePath), QDir(tgtFilePath), visited);
        else if(entry.isFile())
        {
            FileCopier::copyFileSafely(srcFilePath, tgtFilePath);
            visited->insert(srcFilePath, true);
            emit progressUpdated(*visited);
        }
        else
            qWarning() << "Unknown file type skipped:" << srcFilePath;
    }
    visited->insert(srcPath, true);
    return true;
}

void FileCopier::copyDirectoryRecursively(const QDir &source, const QDir &target)
{
    QMap<QString, bool> visited;
    bool success = _copyDirectoryRecursively(source, target, &visited);
    emit copyFinished(success);
}

int FileCopier::getNumberOfFilesRecursive(const QDir &source)
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

bool FileCopier::copyFileSafely(const QString &src, const QString &dst)
{
    QString backup = dst + ".bak";
    if (QFile::exists(dst)) {
        QFile::remove(backup);
        if (!QFile::rename(dst, backup))
        {
            qWarning() << "Failed to backup existing file:" << dst;
            return false;
        }
    }

    if (!QFile::copy(src, dst))
    {
        QFile::rename(backup, dst);
        qWarning() << "Failed to copy" << src << "to" << dst << "restoring backup...";
        return false;
    }

    QFile::remove(backup);
    return true;
}
