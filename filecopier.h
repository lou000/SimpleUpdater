#ifndef FILECOPIER_H
#define FILECOPIER_H

#include <QDir>
#include <QObject>

class FileCopier : public QObject {
    Q_OBJECT
public:
    explicit FileCopier(QObject* parent = nullptr) : QObject(parent) {}

    void copyDirectoryRecursively(const QDir &source, const QDir &target);
    static bool copyFileSafely(const QString& src, const QString& dst);
    static int getNumberOfFilesRecursive(const QDir &source);
public slots:
    void cancel(){cancelRequested.store(true);}

signals:
    void progressUpdated(QMap<QString, bool> success);
    void copyFinished();
    void cancelled();

private:
    std::atomic<bool> cancelRequested = false;
    std::atomic<bool> canceled = false;
    bool _copyDirectoryRecursively(const QDir &source, const QDir &target, QMap<QString, bool> *visited);
};


#endif // FILECOPIER_H
