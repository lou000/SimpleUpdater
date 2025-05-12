#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QDir>
#include <QObject>
#include <QVersionNumber>

//TODO: decouple copyfinished from here!, move backups to main application, TESTING
class QSettings;
class FileHandler : public QObject {
    Q_OBJECT
public:
    explicit FileHandler(QObject* parent = nullptr);

    bool copyDirectoryRecursively(QDir source, QDir target);
    bool copyFiles(QDir source, QDir target, QStringList filePaths, bool cancelable);
    bool removeFiles(QDir dir, QStringList filePaths);
    static int getFileCountRecursive(const QDir &source);
    static QByteArray hashFile(QFile& file);
    static void generateInfoFile(const QDir& directory, const QVersionNumber &version,
                                 const QString& appExe, bool full, bool force);

public slots:
    void cancel(){cancelRequested.store(true);}

signals:
    void progressUpdated(QPair<QString, bool> success);
    void cancelled();

private:
    std::atomic<bool> cancelRequested = false;
    std::atomic<bool> canceled = false;
    bool _copyDirectoryRecursively(const QDir &source, const QDir &target, QSet<QString>* visited);
};


#endif // FILEHANDLER_H
