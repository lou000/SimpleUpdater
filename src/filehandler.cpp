#include "filehandler.h"
#include "platform/platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

FileHandler::FileHandler(QObject* parent)
    : QObject(parent)
    , m_selfPath(QCoreApplication::applicationFilePath())
{
}

void FileHandler::setLockResolver(LockResolverCallback callback)
{
    m_lockResolver = std::move(callback);
}

bool FileHandler::retryWithLockResolver(const QString& absolutePath,
                                        const std::function<bool()>& operation)
{
    while(true)
    {
        if(operation())
            return true;

        if(!m_lockResolver || !Platform::isFileLockError())
            return false;

        if(!m_lockResolver(absolutePath))
            return false;
    }
}

FileDiff FileHandler::computeDiff(const QHash<QString, QByteArray>& sourceFiles,
                                  const QHash<QString, QByteArray>& targetFiles)
{
    FileDiff diff;

    for(auto it = sourceFiles.constBegin(); it != sourceFiles.constEnd(); ++it)
    {
        if(!targetFiles.contains(it.key()))
            diff.toAdd.append(it.key());
        else if(targetFiles.value(it.key()) != it.value())
            diff.toUpdate.append(it.key());
        else
            diff.unchanged.append(it.key());
    }

    for(auto it = targetFiles.constBegin(); it != targetFiles.constEnd(); ++it)
    {
        if(!sourceFiles.contains(it.key()))
            diff.toRemove.append(it.key());
    }

    return diff;
}

bool FileHandler::copyFiles(const QDir& source, const QDir& target, const QStringList& relativePaths)
{
    bool overallSuccess = true;

    for(const auto& relPath : relativePaths)
    {
        if(checkCancel())
            return false;

        QString srcPath = source.filePath(relPath);
        QString tgtPath = target.filePath(relPath);

        if(isSelf(tgtPath))
        {
            emit progressUpdated(relPath + " (SKIP self)", true);
            continue;
        }

        if(!QFileInfo::exists(srcPath))
        {
            qWarning() << "Source file does not exist:" << srcPath;
            emit progressUpdated(relPath + " (COPY) - source not found", false);
            overallSuccess = false;
            continue;
        }

        QDir tgtDir = QFileInfo(tgtPath).absoluteDir();
        if(!tgtDir.exists() && !tgtDir.mkpath("."))
        {
            qWarning() << "Failed to create target directory:" << tgtDir.absolutePath();
            emit progressUpdated(relPath + " (COPY) - cannot create directory", false);
            overallSuccess = false;
            continue;
        }

        if(QFile::exists(tgtPath))
        {
            bool removed = retryWithLockResolver(tgtPath, [&](){
                return QFile::remove(tgtPath);
            });
            if(!removed)
            {
                qWarning() << "Failed to remove existing file:" << tgtPath;
                emit progressUpdated(relPath + " (COPY) - cannot remove existing", false);
                overallSuccess = false;
                continue;
            }
        }

        bool copied = retryWithLockResolver(tgtPath, [&](){
            return QFile::copy(srcPath, tgtPath);
        });
        if(!copied)
        {
            qWarning() << "Failed to copy" << srcPath << "to" << tgtPath;
            emit progressUpdated(relPath + " (COPY)", false);
            overallSuccess = false;
            continue;
        }

        QFile::setPermissions(tgtPath, QFileInfo(srcPath).permissions());
        emit progressUpdated(relPath + " (COPY)", true);
    }

    return overallSuccess;
}

bool FileHandler::removeFiles(const QDir& directory, const QStringList& relativePaths)
{
    bool overallSuccess = true;

    for(const auto& relPath : relativePaths)
    {
        if(checkCancel())
            return false;

        QString fullPath = directory.filePath(relPath);

        if(isSelf(fullPath))
        {
            emit progressUpdated(relPath + " (SKIP self)", true);
            continue;
        }

        if(!QFileInfo::exists(fullPath))
        {
            emit progressUpdated(relPath + " (REMOVE) - already gone", true);
            continue;
        }

        bool removed = retryWithLockResolver(fullPath, [&](){
            return QFile::remove(fullPath);
        });
        if(!removed)
        {
            qWarning() << "Failed to remove:" << fullPath;
            emit progressUpdated(relPath + " (REMOVE)", false);
            overallSuccess = false;
        }
        else
        {
            emit progressUpdated(relPath + " (REMOVE)", true);
        }
    }

    return overallSuccess;
}

bool FileHandler::renameToBackup(const QDir& directory, const QStringList& relativePaths)
{
    for(int i = 0; i < relativePaths.size(); ++i)
    {
        const auto& relPath = relativePaths[i];
        QString path = directory.filePath(relPath);
        QString bakPath = path + ".bak";

        if(QFile::exists(bakPath))
        {
            retryWithLockResolver(bakPath, [&](){
                return QFile::remove(bakPath);
            });
        }

        if(!QFile::exists(path))
        {
            emit progressUpdated(relPath + " (BACKUP) - not found, skipping", true);
            continue;
        }

        bool renamed = retryWithLockResolver(path, [&](){
            return QFile::rename(path, bakPath);
        });
        if(!renamed)
        {
            qWarning() << "Failed to rename" << path << "to" << bakPath;
            emit progressUpdated(relPath + " (BACKUP)", false);

            for(int j = 0; j < i; ++j)
            {
                QString prevPath = directory.filePath(relativePaths[j]);
                QString prevBak = prevPath + ".bak";
                if(QFile::exists(prevBak))
                    QFile::rename(prevBak, prevPath);
            }
            return false;
        }

        emit progressUpdated(relPath + " (BACKUP)", true);
    }

    return true;
}

bool FileHandler::restoreFromBackup(const QDir& directory, const QStringList& relativePaths)
{
    bool overallSuccess = true;

    for(const auto& relPath : relativePaths)
    {
        QString path = directory.filePath(relPath);
        QString bakPath = path + ".bak";

        if(!QFile::exists(bakPath))
            continue;

        if(QFile::exists(path))
        {
            retryWithLockResolver(path, [&](){
                return QFile::remove(path);
            });
        }

        bool renamed = retryWithLockResolver(bakPath, [&](){
            return QFile::rename(bakPath, path);
        });
        if(!renamed)
        {
            qWarning() << "Failed to restore" << bakPath << "to" << path;
            emit progressUpdated(relPath + " (RESTORE)", false);
            overallSuccess = false;
        }
        else
        {
            emit progressUpdated(relPath + " (RESTORE)", true);
        }
    }

    return overallSuccess;
}

void FileHandler::cleanupBackups(const QDir& directory, const QStringList& relativePaths)
{
    for(const auto& relPath : relativePaths)
    {
        QString bakPath = directory.filePath(relPath) + ".bak";
        if(QFile::exists(bakPath))
            QFile::remove(bakPath);
    }
}

QStringList FileHandler::verifyFiles(const QDir& directory,
                                     const QHash<QString, QByteArray>& expectedHashes)
{
    QStringList mismatches;

    for(auto it = expectedHashes.constBegin(); it != expectedHashes.constEnd(); ++it)
    {
        QString fullPath = directory.filePath(it.key());
        QByteArray actual;
        bool hashed = retryWithLockResolver(fullPath, [&](){
            actual = hashFile(fullPath);
            return !actual.isEmpty();
        });
        if(!hashed || actual != it.value())
            mismatches.append(it.key());
    }

    return mismatches;
}

void FileHandler::removeEmptyDirectories(const QDir& directory)
{
    QStringList dirs;
    QDirIterator it(directory.absolutePath(),
                    QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    while(it.hasNext())
    {
        it.next();
        dirs.append(it.filePath());
    }

    std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b){
        return a.length() > b.length();
    });

    for(const auto& dirPath : dirs)
    {
        if(dirPath == directory.absolutePath())
            continue;

        QDir dir(dirPath);
        if(dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
            QDir().rmdir(dirPath);
    }
}

QByteArray FileHandler::hashFile(const QString& filePath)
{
    QFile file(filePath);
    if(!file.open(QFile::ReadOnly))
    {
        qWarning() << "Failed to open" << filePath << "for reading:" << file.errorString();
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();
    return hash.result();
}

void FileHandler::cancel()
{
    m_cancelRequested.store(true);
}

void FileHandler::resetCancel()
{
    m_cancelRequested.store(false);
}

bool FileHandler::isCancelled() const
{
    return m_cancelRequested.load();
}

bool FileHandler::isSelf(const QString& absolutePath) const
{
    return QFileInfo(absolutePath) == QFileInfo(m_selfPath);
}

bool FileHandler::checkCancel()
{
    if(m_cancelRequested.load())
    {
        emit cancelled();
        return true;
    }
    return false;
}
