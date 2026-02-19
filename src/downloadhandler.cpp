#include "downloadhandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QUuid>

static const int kMaxRetries = 3;
static const int kRetryDelayMs = 2000;
static const int kTransferTimeoutMs = 30000;

static bool isTransientError(QNetworkReply::NetworkError error)
{
    switch(error)
    {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::ServiceUnavailableError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::InternalServerError:
        return true;
    default:
        return false;
    }
}

static QString httpErrorMessage(int statusCode)
{
    switch(statusCode)
    {
    case 403: return "Access denied (HTTP 403). Check credentials or permissions.";
    case 404: return "File not found (HTTP 404). Verify the download URL.";
    case 408: return "Request timed out (HTTP 408).";
    case 429: return "Too many requests (HTTP 429). Try again later.";
    case 500: return "Internal server error (HTTP 500).";
    case 502: return "Bad gateway (HTTP 502).";
    case 503: return "Service unavailable (HTTP 503).";
    default:  return QString("HTTP error %1.").arg(statusCode);
    }
}

DownloadHandler::DownloadHandler(QObject* parent)
    : QObject(parent)
{
}

DownloadHandler::~DownloadHandler()
{
    cleanup();
}

QString DownloadHandler::downloadAndExtract(const QString& url)
{
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(12);
    QString tempDirName = "SimpleUpdater_download_" + uuid;
    QString tempPath = QDir::temp().filePath(tempDirName);

    if(!QDir::temp().mkpath(tempDirName))
    {
        emit statusMessage("Failed to create temporary directory: " + tempPath);
        return {};
    }
    m_tempDir = tempPath;

    emit statusMessage("Downloading: " + url);
    QString filePath = download(url);
    if(filePath.isEmpty())
        return {};

    QFileInfo fi(filePath);
    QString extractDir = m_tempDir + "/extracted";

    if(fi.suffix().compare("zip", Qt::CaseInsensitive) == 0)
    {
        emit statusMessage("Extracting archive...");
        if(!QDir().mkpath(extractDir))
        {
            emit statusMessage("Failed to create extraction directory.");
            return {};
        }
        if(!extractZip(filePath, extractDir))
            return {};
    }
    else
    {
        // Not a zip; treat the downloaded file's directory as source.
        // For non-archive downloads we expect the URL to point at a directory listing
        // or a single manifest -- this is an uncommon path.
        extractDir = m_tempDir;
    }

    QString root = findManifestRoot(extractDir);
    if(root.isEmpty())
    {
        emit statusMessage("Downloaded content does not contain manifest.json. "
                           "Ensure the archive contains a valid update package.");
        return {};
    }

    emit statusMessage("Download ready: " + root);
    return root;
}

void DownloadHandler::cleanup()
{
    if(!m_tempDir.isEmpty())
    {
        QDir(m_tempDir).removeRecursively();
        m_tempDir.clear();
    }
}

QString DownloadHandler::download(const QString& url)
{
    QNetworkAccessManager nam;
    QUrl qurl(url);

    if(!qurl.isValid() || qurl.scheme().isEmpty())
    {
        emit statusMessage("Invalid URL: " + url);
        return {};
    }

    // Derive filename from URL path, fall back to "download"
    QString filename = QFileInfo(qurl.path()).fileName();
    if(filename.isEmpty())
        filename = "download";
    QString destPath = m_tempDir + "/" + filename;

    for(int attempt = 1; attempt <= kMaxRetries; ++attempt)
    {
        if(attempt > 1)
        {
            emit statusMessage(QString("Retry %1/%2...").arg(attempt).arg(kMaxRetries));
            QEventLoop waitLoop;
            QTimer::singleShot(kRetryDelayMs, &waitLoop, &QEventLoop::quit);
            waitLoop.exec();
        }

        QNetworkRequest request(qurl);
        request.setTransferTimeout(kTransferTimeoutMs);

        QNetworkReply* reply = nam.get(request);

        connect(reply, &QNetworkReply::downloadProgress,
                this, &DownloadHandler::downloadProgress);

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if(reply->error() != QNetworkReply::NoError)
        {
            auto replyError = reply->error();
            QString errMsg = reply->errorString();
            bool transient = isTransientError(replyError);
            reply->deleteLater();

            if(transient && attempt < kMaxRetries)
            {
                emit statusMessage("Download failed (transient): " + errMsg);
                continue;
            }

            if(replyError == QNetworkReply::TimeoutError)
                emit statusMessage("Download timed out after " + QString::number(kTransferTimeoutMs / 1000) + " seconds.");
            else
                emit statusMessage("Download failed: " + errMsg);
            return {};
        }

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(statusCode != 200)
        {
            bool transient = (statusCode == 408 || statusCode == 429
                              || statusCode == 500 || statusCode == 502
                              || statusCode == 503);
            reply->deleteLater();

            if(transient && attempt < kMaxRetries)
            {
                emit statusMessage(httpErrorMessage(statusCode) + " Retrying...");
                continue;
            }

            emit statusMessage(httpErrorMessage(statusCode));
            return {};
        }

        QFile outFile(destPath);
        if(!outFile.open(QIODevice::WriteOnly))
        {
            emit statusMessage("Failed to write downloaded file: " + destPath);
            reply->deleteLater();
            return {};
        }
        outFile.write(reply->readAll());
        outFile.close();
        reply->deleteLater();

        emit statusMessage("Download complete: " + filename
                           + " (" + QString::number(QFileInfo(destPath).size() / 1024) + " KB)");
        return destPath;
    }

    return {};
}

bool DownloadHandler::extractZip(const QString& zipPath, const QString& destDir)
{
#ifdef Q_OS_WIN
    QProcess proc;
    proc.setWorkingDirectory(destDir);
    proc.start("powershell", {
        "-NoProfile", "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir))
    });
    proc.waitForFinished(120000);
    if(proc.exitCode() != 0)
    {
        QString err = proc.readAllStandardError().trimmed();
        emit statusMessage("Extraction failed: " + (err.isEmpty() ? "unknown error" : err));
        return false;
    }
#else
    QProcess proc;
    proc.setWorkingDirectory(destDir);
    proc.start("unzip", {"-o", zipPath, "-d", destDir});
    proc.waitForFinished(120000);
    if(proc.exitCode() != 0)
    {
        QString err = proc.readAllStandardError().trimmed();
        emit statusMessage("Extraction failed: " + (err.isEmpty() ? "unknown error" : err));
        return false;
    }
#endif
    return true;
}

QString DownloadHandler::findManifestRoot(const QString& dir)
{
    // Check current directory first
    if(QFile::exists(dir + "/manifest.json"))
        return dir;

    // Check one level of subdirectories (common pattern: zip contains a single folder)
    QDir d(dir);
    auto entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for(const auto& sub : entries)
    {
        QString subPath = d.filePath(sub);
        if(QFile::exists(subPath + "/manifest.json"))
            return subPath;
    }

    return {};
}
