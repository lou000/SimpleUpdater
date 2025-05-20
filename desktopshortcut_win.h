#ifndef DESKTOPSHORTCUT_WIN_H
#define DESKTOPSHORTCUT_WIN_H

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <comdef.h>
#include <wrl/client.h>
#include <QDir>
#include <QStandardPaths>

inline bool createShortcut(const QString& targetPath, const QString& shortcutName, const QString& iconPath = {})
{
    using namespace Microsoft::WRL;

    CoInitialize(nullptr);
    ComPtr<IShellLink> shellLink;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))))
        return false;

    shellLink->SetPath(reinterpret_cast<const wchar_t *>(targetPath.utf16()));

    if (!iconPath.isEmpty())
        shellLink->SetIconLocation(reinterpret_cast<const wchar_t *>(iconPath.utf16()), 0);

    QString targetDir = QFileInfo(targetPath).absolutePath();
    shellLink->SetWorkingDirectory(reinterpret_cast<const wchar_t *>(targetDir.utf16()));

    ComPtr<IPersistFile> persistFile;
    if (FAILED(shellLink.As(&persistFile)))
        return false;

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fullPath = desktopPath + "/" + shortcutName + ".lnk";
    HRESULT hr = persistFile->Save(reinterpret_cast<const wchar_t *>(fullPath.utf16()), TRUE);

    CoUninitialize();
    return SUCCEEDED(hr);
}

#endif // DESKTOPSHORTCUT_WIN_H
