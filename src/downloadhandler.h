#ifndef DOWNLOADHANDLER_H
#define DOWNLOADHANDLER_H

#include <QDir>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

class DownloadHandler : public QObject {
    Q_OBJECT
public:
    explicit DownloadHandler(QObject* parent = nullptr);
    ~DownloadHandler();

    // Download URL to a temp directory. Extracts .zip if applicable.
    // Returns the local directory path on success, empty string on failure.
    // This is a blocking call (runs its own event loop for network I/O).
    QString downloadAndExtract(const QString& url);

    // Clean up the temp directory created by downloadAndExtract.
    void cleanup();

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void statusMessage(const QString& msg);

private:
    QString m_tempDir;

    QString download(const QString& url);
    bool extractZip(const QString& zipPath, const QString& destDir);
    QString findManifestRoot(const QString& dir);
};

#endif // DOWNLOADHANDLER_H
