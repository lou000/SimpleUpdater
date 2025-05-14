#ifndef DESKTOPSHORTCUT_LINUX_H
#define DESKTOPSHORTCUT_LINUX_H

#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QFileDevice>

inline bool createShortcut(const QString& targetPath, const QString& shortcutName, const QString& iconPath = {})
{
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString shortcutPath = desktopPath + "/" + shortcutName + ".desktop";

    QFile file(shortcutPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Version=1.0\n";
    out << "Type=Application\n";
    out << "Name=" << shortcutName << "\n";
    out << "Exec=" << targetPath << "\n";

    // Only set the icon if it is provided
    if (!iconPath.isEmpty())
        out << "Icon=" << iconPath << "\n";

    out << "Terminal=false\n";
    file.close();

    return QFile(shortcutPath).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
}

#endif // DESKTOPSHORTCUT_LINUX_H
