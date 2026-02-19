#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QDir>
#include <QHash>
#include <QObject>
#include <functional>

struct FileDiff {
    QStringList toAdd;      // relative paths in source but not in target
    QStringList toUpdate;   // relative paths in both but hash differs
    QStringList toRemove;   // relative paths in target but not in source
    QStringList unchanged;  // relative paths with matching hashes
};

class FileHandler : public QObject {
    Q_OBJECT
public:
    using LockResolverCallback = std::function<bool(const QString& absolutePath)>;

    explicit FileHandler(QObject* parent = nullptr);

    void setLockResolver(LockResolverCallback callback);

    // Compute diff between two file manifests.
    static FileDiff computeDiff(const QHash<QString, QByteArray>& sourceFiles,
                                const QHash<QString, QByteArray>& targetFiles);

    // Copy specific files from source to target by relative path.
    // Creates subdirectories as needed. Skips the updater's own exe.
    // Emits progressUpdated for each file. Returns false if any file fails.
    bool copyFiles(const QDir& source, const QDir& target, const QStringList& relativePaths);

    // Remove specific files from a directory by relative path.
    // Emits progressUpdated for each file.
    // Returns false if any file fails to remove.
    bool removeFiles(const QDir& directory, const QStringList& relativePaths);

    // Rename files to .bak in preparation for staging apply.
    // For each relativePath in directory: rename path to path.bak
    // Returns false if any rename fails (rolls back already-renamed files).
    bool renameToBackup(const QDir& directory, const QStringList& relativePaths);

    // Restore .bak files (rollback). Idempotent -- only touches files with .bak counterparts.
    // For each relativePath: if path.bak exists, remove path, rename .bak back.
    bool restoreFromBackup(const QDir& directory, const QStringList& relativePaths);

    // Delete .bak files (cleanup after successful apply).
    void cleanupBackups(const QDir& directory, const QStringList& relativePaths);

    // Verify that files in directory match expected hashes.
    // Returns list of relative paths that DON'T match (empty = all good).
    QStringList verifyFiles(const QDir& directory, const QHash<QString, QByteArray>& expectedHashes);

    // Remove empty directories recursively (bottom-up). Never removes the root itself.
    void removeEmptyDirectories(const QDir& directory);

    // Hash a single file. Returns empty QByteArray on failure.
    static QByteArray hashFile(const QString& filePath);

    // Request cancellation. Thread-safe.
    void cancel();

    // Reset cancellation state. Call before starting a new operation.
    void resetCancel();

    // Check if cancellation was requested.
    bool isCancelled() const;

signals:
    void progressUpdated(const QString& description, bool success);
    void cancelled();

private:
    std::atomic<bool> m_cancelRequested{false};
    LockResolverCallback m_lockResolver;

    QString m_selfPath;

    bool isSelf(const QString& absolutePath) const;
    bool checkCancel();
    bool retryWithLockResolver(const QString& absolutePath,
                               const std::function<bool()>& operation);
};

#endif // FILEHANDLER_H
