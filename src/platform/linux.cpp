#include "platform.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <cerrno>
#include <signal.h>

namespace Platform {

bool createShortcut(const QString& targetExePath, const QString& shortcutName,
                    const QString& iconPath)
{
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString shortcutPath = desktopPath + "/" + shortcutName + ".desktop";

    QFile file(shortcutPath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Version=1.0\n";
    out << "Type=Application\n";
    out << "Name=" << shortcutName << "\n";
    out << "Exec=" << targetExePath << "\n";

    if(!iconPath.isEmpty())
        out << "Icon=" << iconPath << "\n";

    out << "Terminal=false\n";
    file.close();

    return QFile(shortcutPath).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
}

bool removeShortcut(const QString& shortcutName)
{
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QFile::remove(desktopPath + "/" + shortcutName + ".desktop");
}

std::optional<QVersionNumber> readExeVersion(const QString& exePath)
{
    QProcess proc;
    proc.setProgram(exePath);
    proc.setArguments({"--version"});
    proc.start(QIODevice::ReadOnly);

    if(!proc.waitForFinished(5000))
    {
        proc.kill();
        return std::nullopt;
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    if(output.isEmpty())
        output = QString::fromUtf8(proc.readAllStandardError());

    static const QRegularExpression re(R"((\d+\.\d+(?:\.\d+)*))");
    auto match = re.match(output);
    if(!match.hasMatch())
        return std::nullopt;

    auto ver = QVersionNumber::fromString(match.captured(1));
    if(ver.isNull())
        return std::nullopt;

    return ver;
}

QList<LockedProcess> findLockingProcesses(const QStringList& absolutePaths)
{
    QList<LockedProcess> result;
    if(absolutePaths.isEmpty())
        return result;

    QSet<QString> pathSet(absolutePaths.begin(), absolutePaths.end());
    QSet<quint64> seenPids;

    QDir procDir("/proc");
    auto entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for(const auto& entry : entries)
    {
        bool ok = false;
        quint64 pid = entry.toULongLong(&ok);
        if(!ok)
            continue;

        QString fdDir = QString("/proc/%1/fd").arg(pid);
        QDir fd(fdDir);
        if(!fd.exists())
            continue;

        auto fds = fd.entryList(QDir::NoDotAndDotDot);
        for(const auto& fdEntry : fds)
        {
            QFileInfo fi(fdDir + "/" + fdEntry);
            if(!fi.isSymLink())
                continue;

            if(pathSet.contains(fi.symLinkTarget()) && !seenPids.contains(pid))
            {
                seenPids.insert(pid);

                QString name = QString::number(pid);
                QFile commFile(QString("/proc/%1/comm").arg(pid));
                if(commFile.open(QFile::ReadOnly))
                {
                    name = QString::fromUtf8(commFile.readAll()).trimmed();
                    commFile.close();
                }

                LockedProcess lp;
                lp.pid = pid;
                lp.name = name;
                result.append(lp);
                break;
            }
        }
    }

    return result;
}

bool killProcess(quint64 pid)
{
    return ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
}

bool isFileLockError()
{
    return errno == ETXTBSY || errno == EBUSY;
}

bool renameSelfForUpdate(const QString& selfPath)
{
    QString oldPath = selfPath + "_old";
    if(QFile::exists(oldPath))
        QFile::remove(oldPath);
    return QFile::rename(selfPath, oldPath);
}

bool cleanupOldSelf(const QString& selfPath)
{
    QString oldPath = selfPath + "_old";
    if(QFile::exists(oldPath))
        return QFile::remove(oldPath);
    return true;
}

bool setExecutablePermission(const QString& path)
{
    QFileInfo fi(path);
    auto perms = fi.permissions();
    return QFile::setPermissions(path, perms | QFileDevice::ExeOwner
                                             | QFileDevice::ExeGroup
                                             | QFileDevice::ExeOther);
}

} // namespace Platform
