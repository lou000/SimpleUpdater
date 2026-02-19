#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVersionNumber>
#include <optional>

namespace Platform {

bool createShortcut(const QString& targetExePath, const QString& shortcutName,
                    const QString& iconPath = {});
bool removeShortcut(const QString& shortcutName);

std::optional<QVersionNumber> readExeVersion(const QString& exePath);

struct LockedProcess {
    quint64 pid;
    QString name;
};

QList<LockedProcess> findLockingProcesses(const QStringList& absolutePaths);
bool killProcess(quint64 pid);

bool isFileLockError();

bool renameSelfForUpdate(const QString& selfPath);
bool cleanupOldSelf(const QString& selfPath);
bool setExecutablePermission(const QString& path);

} // namespace Platform
