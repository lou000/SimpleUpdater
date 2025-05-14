#ifndef DESKTOPSHORTCUT_H
#define DESKTOPSHORTCUT_H

#if defined(_WIN32)
#include "desktopshortcut_win.h"  // IWYU pragma: export
#elif defined(__linux__)
#include "desktopshortcut_linux.h"  // IWYU pragma: export
#else
inline bool createShortcut(const QString& targetPath, const QString& shortcutName, const QString& iconPath = {})
{
    return false; // No-op for unsupported platforms
}
#endif

#endif // DESKTOPSHORTCUT_H
