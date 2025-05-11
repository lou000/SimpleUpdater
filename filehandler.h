#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QDir>
#include <QObject>
#include <QVersionNumber>

class QSettings;
class FileHandler : public QObject {
    Q_OBJECT
public:
    explicit FileHandler(QObject* parent = nullptr);

    void copyDirectoryRecursively(const QDir &source, const QDir &target);
    void copyFiles(const QDir &source, const QDir &target, QList<QString> filePaths);
    static bool copyFileSafely(const QString& src, const QString& dst);
    static int getNumberOfFilesRecursive(const QDir &source);
    static QByteArray hashFile(QFile& file);
    static void generateInfoFile(const QDir& directory, const QVersionNumber &version);

public slots:
    void cancel(){cancelRequested.store(true);}

signals:
    void progressUpdated(QPair<QString, bool> success);
    void copyFinished(bool success);
    void cancelled();

private:
    std::atomic<bool> cancelRequested = false;
    std::atomic<bool> canceled = false;
    bool _copyDirectoryRecursively(const QDir &source, const QDir &target, QSet<QString>* visited);
};


#endif // FILEHANDLER_H
