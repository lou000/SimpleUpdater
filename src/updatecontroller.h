#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include "manifest.h"
#include "filehandler.h"
#include <QColor>
#include <QDir>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

class DownloadHandler;

enum class LockAction { Retry, KillAll, Cancel };

class UpdateController : public QObject {
    Q_OBJECT
public:
    explicit UpdateController(QObject* parent = nullptr);

    void setSourceDir(const QDir& dir);
    void setSourceUrl(const QString& url);
    void setTargetDir(const QDir& dir);
    void setForceUpdate(bool force);
    void setInstallMode(bool install);
    void setContinueUpdate(bool continueUpdate);

    // Resolve source URL to a local directory. Must be called before prepare()
    // when the source is a URL. Returns true on success.
    bool resolveSource();

    void prepare();

    // Execute the full update flow (called from worker thread).
    void execute();

    // Cancel the current operation. Thread-safe.
    void cancel();

    // Called by UI to respond to a processLockDetected signal.
    void respondToLockPrompt(LockAction action);

    // Accessors for UI to read after prepare()
    const Manifest& sourceManifest() const;
    QVersionNumber targetVersion() const;
    const FileDiff& fileDiff() const;
    bool isMandatory() const;
    bool isInstall() const;
    bool isCancelled() const;
    QDir targetDir() const;

    // Clean up any temporary download directory.
    void cleanupDownload();

signals:
    void updateReady();
    void statusMessage(const QString& msg, const QColor& color);
    void progressUpdated(const QString& description, bool success);
    void progressRangeChanged(int min, int max);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updateFinished(bool success);
    void error(const QString& message);
    void processLockDetected(const QStringList& processes);
    void selfUpdateRelaunch();

private:
    QDir m_sourceDir;
    QString m_sourceUrl;
    QDir m_targetDir;
    bool m_forceUpdate = false;
    bool m_installMode = false;
    bool m_continueUpdate = false;
    bool m_mandatory = false;
    FileHandler* m_fileHandler;
    DownloadHandler* m_downloadHandler = nullptr;
    Manifest m_sourceManifest;
    QVersionNumber m_targetVersion;
    QHash<QString, QByteArray> m_targetFiles;
    FileDiff m_diff;

    QMutex m_lockMutex;
    QWaitCondition m_lockCondition;
    LockAction m_lockResponse = LockAction::Retry;

    bool applyStaged(const QDir& stagingDir, const QStringList& filesToStage);
    bool resolveFileLock(const QString& absolutePath);
};

#endif // UPDATECONTROLLER_H
