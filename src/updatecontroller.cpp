#include "updatecontroller.h"
#include "downloadhandler.h"
#include "platform/platform.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

UpdateController::UpdateController(QObject* parent)
    : QObject(parent)
    , m_fileHandler(new FileHandler(this))
{
    connect(m_fileHandler, &FileHandler::progressUpdated,
            this, &UpdateController::progressUpdated);

    m_fileHandler->setLockResolver([this](const QString& absolutePath) -> bool {
        return resolveFileLock(absolutePath);
    });
}

void UpdateController::setSourceDir(const QDir& dir) { m_sourceDir = dir; m_sourceUrl.clear(); }
void UpdateController::setSourceUrl(const QString& url) { m_sourceUrl = url; }
void UpdateController::setTargetDir(const QDir& dir) { m_targetDir = dir; }
void UpdateController::setForceUpdate(bool force) { m_forceUpdate = force; }
void UpdateController::setInstallMode(bool install) { m_installMode = install; }
void UpdateController::setContinueUpdate(bool continueUpdate) { m_continueUpdate = continueUpdate; }

bool UpdateController::resolveSource()
{
    if(m_sourceUrl.isEmpty())
        return true;

    if(!m_downloadHandler)
    {
        m_downloadHandler = new DownloadHandler(this);
        connect(m_downloadHandler, &DownloadHandler::downloadProgress,
                this, &UpdateController::downloadProgress);
        connect(m_downloadHandler, &DownloadHandler::statusMessage,
                this, [this](const QString& msg){ emit statusMessage(msg, Qt::cyan); });
    }

    QString localPath = m_downloadHandler->downloadAndExtract(m_sourceUrl);
    if(localPath.isEmpty())
    {
        emit error("Failed to download update package from: " + m_sourceUrl);
        return false;
    }

    m_sourceDir = QDir(localPath);
    return true;
}

void UpdateController::cleanupDownload()
{
    if(m_downloadHandler)
        m_downloadHandler->cleanup();
}

const Manifest& UpdateController::sourceManifest() const { return m_sourceManifest; }
QVersionNumber UpdateController::targetVersion() const { return m_targetVersion; }
const FileDiff& UpdateController::fileDiff() const { return m_diff; }
bool UpdateController::isMandatory() const { return m_mandatory; }
bool UpdateController::isInstall() const { return m_installMode; }
bool UpdateController::isCancelled() const { return m_fileHandler->isCancelled(); }
QDir UpdateController::targetDir() const { return m_targetDir; }
QDir UpdateController::sourceDir() const { return m_sourceDir; }

void UpdateController::cancel()
{
    m_fileHandler->cancel();
    QMutexLocker locker(&m_lockMutex);
    m_lockResponse = LockAction::Cancel;
    m_lockCondition.wakeOne();
}

void UpdateController::respondToLockPrompt(LockAction action)
{
    QMutexLocker locker(&m_lockMutex);
    m_lockResponse = action;
    m_lockCondition.wakeOne();
}

void UpdateController::prepare()
{
    if(!m_sourceUrl.isEmpty() && !m_sourceDir.exists())
    {
        emit updateReady();
        return;
    }

    auto srcManifest = readManifest(m_sourceDir.filePath("manifest.json"));
    if(srcManifest)
    {
        m_sourceManifest = srcManifest.value();
    }
    else
    {
        Manifest m;
        m.files = hashDirectory(m_sourceDir);
        m_sourceManifest = m;
    }

    m_targetVersion = QVersionNumber();

    if(m_targetDir.exists() && !m_sourceManifest.appExe.isEmpty())
    {
        QString targetExePath = m_targetDir.absoluteFilePath(m_sourceManifest.appExe);
        if(QFileInfo::exists(targetExePath))
        {
            auto ver = Platform::readExeVersion(targetExePath);
            if(ver)
                m_targetVersion = *ver;
            else
                qWarning() << "Cannot read version from target exe, forcing update:"
                           << targetExePath;
        }
        else
        {
            qWarning() << "Target exe not found, forcing update:" << targetExePath;
        }
    }

    m_mandatory = m_forceUpdate;
    if(!m_mandatory)
    {
        if(m_targetVersion.isNull())
            m_mandatory = true;
        else if(m_sourceManifest.minVersion
                && m_targetVersion < m_sourceManifest.minVersion.value())
            m_mandatory = true;
    }

    emit updateReady();
}

void UpdateController::hashTargetWithLockRetry()
{
    m_targetFiles.clear();
    if(!m_targetDir.exists())
        return;

    m_targetFiles = hashDirectory(m_targetDir);

    while(true)
    {
        QStringList unhashed;
        QDirIterator it(m_targetDir.absolutePath(),
                        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while(it.hasNext())
        {
            it.next();
            QFileInfo info = it.fileInfo();
            if(info.isSymLink())
                continue;

            QString fileName = info.fileName();
            if(fileName == "manifest.json" || fileName == "manifest.json.tmp"
               || fileName == "updateInfo.ini")
                continue;

            QString relPath = m_targetDir.relativeFilePath(info.absoluteFilePath());
            if(!m_targetFiles.contains(relPath))
                unhashed << info.absoluteFilePath();
        }

        if(unhashed.isEmpty())
            break;

        auto locked = Platform::findLockingProcesses(unhashed);
        if(locked.isEmpty())
            break;

        QStringList descriptions;
        for(const auto& p : locked)
            descriptions << QString("%1 (PID %2)").arg(p.name).arg(p.pid);

        emit processLockDetected(descriptions);

        LockAction action;
        {
            QMutexLocker locker(&m_lockMutex);
            m_lockResponse = LockAction::Retry;
            m_lockCondition.wait(&m_lockMutex);
            action = m_lockResponse;
        }

        if(action == LockAction::Cancel)
        {
            m_fileHandler->cancel();
            break;
        }

        if(action == LockAction::KillAll)
        {
            for(const auto& p : locked)
                Platform::killProcess(p.pid);
            QThread::msleep(500);
        }

        m_targetFiles = hashDirectory(m_targetDir);
    }
}

void UpdateController::execute()
{
    m_fileHandler->resetCancel();

    if(!m_sourceUrl.isEmpty())
    {
        if(!resolveSource())
        {
            emit statusMessage("DOWNLOAD FAILED", Qt::red);
            emit updateFinished(false);
            return;
        }
        prepare();
    }

    emit statusMessage("SCANNING TARGET...", Qt::green);
    hashTargetWithLockRetry();
    m_diff = FileHandler::computeDiff(m_sourceManifest.files, m_targetFiles);

    QString selfPath = QCoreApplication::applicationFilePath();
    QString selfRelPath = m_targetDir.relativeFilePath(selfPath);
    bool selfInsideTarget = !selfRelPath.startsWith("..") && !QDir::isAbsolutePath(selfRelPath);

    if(!m_continueUpdate && selfInsideTarget)
    {
        if(m_diff.toUpdate.contains(selfRelPath) || m_diff.toAdd.contains(selfRelPath))
        {
            emit statusMessage("Self-update detected, relaunching...", Qt::yellow);

            if(!Platform::renameSelfForUpdate(selfPath))
            {
                emit statusMessage("Failed to rename updater for self-update", Qt::red);
                emit updateFinished(false);
                return;
            }

            QString srcSelfPath = m_sourceDir.filePath(selfRelPath);
            if(!QFile::copy(srcSelfPath, selfPath))
            {
                qWarning() << "Failed to copy new updater from" << srcSelfPath << "to" << selfPath;
                QString oldPath = selfPath + "_old";
                if(QFile::exists(oldPath))
                    QFile::rename(oldPath, selfPath);
                emit statusMessage("Failed to copy new updater", Qt::red);
                emit updateFinished(false);
                return;
            }

            Platform::setExecutablePermission(selfPath);

            QStringList args = QCoreApplication::arguments();
            args.removeFirst();
            if(!args.contains("--continue-update"))
                args << "--continue-update";

            bool launched = QProcess::startDetached(selfPath, args, m_targetDir.absolutePath());
            if(!launched)
            {
                emit statusMessage("Failed to relaunch updater", Qt::red);
                emit updateFinished(false);
                return;
            }

            emit selfUpdateRelaunch();
            return;
        }
    }
    else if(m_continueUpdate)
    {
        Platform::cleanupOldSelf(selfPath);
        if(selfInsideTarget)
        {
            m_diff.toUpdate.removeAll(selfRelPath);
            m_diff.toAdd.removeAll(selfRelPath);
        }
    }

    QStringList filesToStage = m_diff.toAdd + m_diff.toUpdate;

    if(filesToStage.isEmpty() && m_diff.toRemove.isEmpty())
    {
        emit statusMessage("Already up to date.", Qt::green);
        emit updateFinished(true);
        return;
    }

    int totalSteps = filesToStage.count()
                   + m_diff.toUpdate.count()
                   + filesToStage.count()
                   + m_diff.toRemove.count();
    emit progressRangeChanged(0, totalSteps);

    emit statusMessage("STAGING FILES...", Qt::green);

    QDir parentDir(m_targetDir);
    parentDir.cdUp();
    QString stagingName = ".staging_" + QString::number(QCoreApplication::applicationPid());
    QDir stagingDir(parentDir.filePath(stagingName));

    if(stagingDir.exists())
        stagingDir.removeRecursively();
    if(!parentDir.mkpath(stagingName))
    {
        emit statusMessage("Failed to create staging directory", Qt::red);
        emit updateFinished(false);
        return;
    }

    if(!m_fileHandler->copyFiles(m_sourceDir, stagingDir, filesToStage))
    {
        if(m_fileHandler->isCancelled())
            emit statusMessage("CANCELLED", Qt::yellow);
        else
            emit statusMessage("STAGING FAILED", Qt::red);
        stagingDir.removeRecursively();
        emit updateFinished(false);
        return;
    }

    emit statusMessage("VERIFYING STAGED FILES...", Qt::green);

    QHash<QString, QByteArray> stagedExpected;
    for(const auto& relPath : filesToStage)
    {
        if(m_sourceManifest.files.contains(relPath))
            stagedExpected.insert(relPath, m_sourceManifest.files.value(relPath));
    }

    if(!stagedExpected.isEmpty())
    {
        QStringList mismatches = m_fileHandler->verifyFiles(stagingDir, stagedExpected);
        if(!mismatches.isEmpty())
        {
            for(const auto& f : mismatches)
                emit statusMessage("Staging mismatch: " + f, Qt::red);
            emit statusMessage("STAGING VERIFICATION FAILED", Qt::red);
            stagingDir.removeRecursively();
            emit updateFinished(false);
            return;
        }
    }

    if(!m_diff.toUpdate.isEmpty())
    {
        emit statusMessage("CREATING BACKUP...", Qt::green);
        if(!m_fileHandler->renameToBackup(m_targetDir, m_diff.toUpdate))
        {
            emit statusMessage("BACKUP FAILED", Qt::red);
            stagingDir.removeRecursively();
            emit updateFinished(false);
            return;
        }
        emit statusMessage("BACKUP SUCCESS", Qt::green);
    }

    emit statusMessage("APPLYING UPDATE...", Qt::green);
    if(!applyStaged(stagingDir, filesToStage))
    {
        emit statusMessage("APPLY FAILED - ROLLING BACK...", Qt::red);
        m_fileHandler->restoreFromBackup(m_targetDir, m_diff.toUpdate);
        stagingDir.removeRecursively();
        emit updateFinished(false);
        return;
    }

    if(!m_diff.toRemove.isEmpty())
    {
        emit statusMessage("REMOVING OBSOLETE FILES...", Qt::green);
        for(const auto& relPath : m_diff.toRemove)
        {
            if(relPath.endsWith(".exe", Qt::CaseInsensitive))
                Platform::removeShortcut(QFileInfo(relPath).completeBaseName());
        }
        m_fileHandler->removeFiles(m_targetDir, m_diff.toRemove);
    }

    {
        emit statusMessage("CLEANING STALE FILES...", Qt::green);

        QDirIterator staleIt(m_targetDir.absolutePath(),
                             QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);

        while(staleIt.hasNext())
        {
            staleIt.next();
            QString absPath = staleIt.filePath();
            QString relPath = m_targetDir.relativeFilePath(absPath);

            if(absPath.endsWith(".bak"))
                continue;

            if(!m_sourceManifest.files.contains(relPath))
            {
                if(relPath.endsWith(".exe", Qt::CaseInsensitive))
                    Platform::removeShortcut(QFileInfo(relPath).completeBaseName());

                bool removed = false;
                while(true)
                {
                    if(QFile::remove(absPath)) { removed = true; break; }
                    if(!Platform::isFileLockError() || !resolveFileLock(absPath)) break;
                }
                if(removed)
                    emit progressUpdated(relPath + " (STALE)", true);
                else
                    emit progressUpdated(relPath + " (STALE) - cannot remove", false);
            }
        }

        m_fileHandler->removeEmptyDirectories(m_targetDir);
    }

    emit statusMessage("VERIFYING TARGET...", Qt::green);
    {
        QStringList mismatches = m_fileHandler->verifyFiles(m_targetDir, m_sourceManifest.files);
        if(!mismatches.isEmpty())
        {
            for(const auto& f : mismatches)
                emit statusMessage("Target mismatch: " + f, Qt::red);
            emit statusMessage("TARGET VERIFICATION FAILED - ROLLING BACK...", Qt::red);
            m_fileHandler->removeFiles(m_targetDir, m_diff.toAdd);
            m_fileHandler->restoreFromBackup(m_targetDir, m_diff.toUpdate);
            stagingDir.removeRecursively();
            emit updateFinished(false);
            return;
        }
    }

    m_fileHandler->cleanupBackups(m_targetDir, m_diff.toUpdate);
    if(stagingDir.exists())
        stagingDir.removeRecursively();

    if(!m_sourceManifest.appExe.isEmpty())
    {
        QString absPath = m_targetDir.absoluteFilePath(m_sourceManifest.appExe);
        QString name = QFileInfo(m_sourceManifest.appExe).completeBaseName();
        Platform::createShortcut(absPath, name);
    }

    if(!m_sourceManifest.appExe.isEmpty())
    {
        QString absPath = m_targetDir.absoluteFilePath(m_sourceManifest.appExe);
        if(QFileInfo::exists(absPath))
        {
            QStringList launchArgs;
            launchArgs << (m_installMode ? "--installation" : "--update");
            bool launched = QProcess::startDetached(absPath, launchArgs, m_targetDir.absolutePath());
            if(launched)
                emit statusMessage("Launching: " + absPath, Qt::yellow);
            else
                emit statusMessage("Failed to launch: " + absPath, Qt::red);
        }
        else
        {
            emit statusMessage("Cannot find application after update: " + absPath, Qt::red);
        }
    }

    cleanupDownload();
    emit updateFinished(true);
}

bool UpdateController::applyStaged(const QDir& stagingDir, const QStringList& filesToStage)
{
    for(const auto& relPath : filesToStage)
    {
        QString srcPath = stagingDir.filePath(relPath);
        QString tgtPath = m_targetDir.filePath(relPath);

        QDir tgtDir = QFileInfo(tgtPath).absoluteDir();
        if(!tgtDir.exists() && !tgtDir.mkpath("."))
        {
            qWarning() << "Failed to create directory:" << tgtDir.absolutePath();
            emit progressUpdated(relPath + " (APPLY) - cannot create directory", false);
            return false;
        }

        if(QFile::exists(tgtPath))
        {
            bool removed = false;
            while(true)
            {
                if(QFile::remove(tgtPath)) { removed = true; break; }
                if(!Platform::isFileLockError() || !resolveFileLock(tgtPath)) break;
            }
            if(!removed)
            {
                emit progressUpdated(relPath + " (APPLY) - cannot remove existing", false);
                return false;
            }
        }

        bool moved = false;
        while(true)
        {
            if(QFile::rename(srcPath, tgtPath)) { moved = true; break; }
            if(!Platform::isFileLockError() || !resolveFileLock(tgtPath)) break;
        }
        if(!moved)
        {
            qWarning() << "Failed to move" << srcPath << "to" << tgtPath;
            emit progressUpdated(relPath + " (APPLY)", false);
            return false;
        }

        emit progressUpdated(relPath + " (APPLY)", true);
    }
    return true;
}

bool UpdateController::resolveFileLock(const QString& absolutePath)
{
    while(true)
    {
        auto locked = Platform::findLockingProcesses({absolutePath});
        if(locked.isEmpty())
            return false;

        QStringList descriptions;
        for(const auto& p : locked)
            descriptions << QString("%1 (PID %2)").arg(p.name).arg(p.pid);

        emit processLockDetected(descriptions);

        LockAction action;
        {
            QMutexLocker locker(&m_lockMutex);
            m_lockResponse = LockAction::Retry;
            m_lockCondition.wait(&m_lockMutex);
            action = m_lockResponse;
        }

        if(action == LockAction::Cancel)
        {
            m_fileHandler->cancel();
            return false;
        }

        if(action == LockAction::KillAll)
        {
            for(const auto& p : locked)
                Platform::killProcess(p.pid);
            QThread::msleep(500);
        }

        auto stillLocked = Platform::findLockingProcesses({absolutePath});
        if(stillLocked.isEmpty())
            return true;
    }
}
