#include "platform.h"

#include <windows.h>
#include <RestartManager.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <wrl/client.h>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStandardPaths>
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Rstrtmgr.lib")

namespace Platform {

bool createShortcut(const QString& targetExePath, const QString& shortcutName,
                    const QString& iconPath)
{
    using namespace Microsoft::WRL;

    CoInitialize(nullptr);
    auto guard = qScopeGuard([]{ CoUninitialize(); });

    ComPtr<IShellLink> shellLink;
    if(FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))))
        return false;

    shellLink->SetPath(reinterpret_cast<const wchar_t*>(targetExePath.utf16()));

    if(!iconPath.isEmpty())
        shellLink->SetIconLocation(reinterpret_cast<const wchar_t*>(iconPath.utf16()), 0);

    QString targetDir = QFileInfo(targetExePath).absolutePath();
    shellLink->SetWorkingDirectory(reinterpret_cast<const wchar_t*>(targetDir.utf16()));

    ComPtr<IPersistFile> persistFile;
    if(FAILED(shellLink.As(&persistFile)))
        return false;

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fullPath = desktopPath + "/" + shortcutName + ".lnk";
    HRESULT hr = persistFile->Save(reinterpret_cast<const wchar_t*>(fullPath.utf16()), TRUE);
    return SUCCEEDED(hr);
}

bool removeShortcut(const QString& shortcutName)
{
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fullPath = desktopPath + "/" + shortcutName + ".lnk";
    return QFile::remove(fullPath);
}

std::optional<QVersionNumber> readExeVersion(const QString& exePath)
{
    std::wstring wpath = exePath.toStdWString();
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(wpath.c_str(), &dummy);
    if(size == 0)
        return std::nullopt;

    QByteArray buffer(static_cast<int>(size), '\0');
    if(!GetFileVersionInfoW(wpath.c_str(), 0, size, buffer.data()))
        return std::nullopt;

    const wchar_t* candidates[] = {
        L"\\StringFileInfo\\040904b0\\ProductVersion",
        L"\\StringFileInfo\\040904E4\\ProductVersion",
    };

    for(const wchar_t* subBlock : candidates)
    {
        LPVOID value = nullptr;
        UINT len = 0;
        if(VerQueryValueW(buffer.data(), subBlock, &value, &len) && len > 0)
        {
            QString versionStr = QString::fromWCharArray(static_cast<const wchar_t*>(value));
            int suffixIndex = 0;
            auto ver = QVersionNumber::fromString(versionStr, &suffixIndex);
            if(!ver.isNull())
                return ver;
        }
    }

    // Fallback: iterate translation table to find any ProductVersion
    struct LANGANDCODEPAGE {
        WORD language;
        WORD codePage;
    } *translations = nullptr;
    UINT transBytes = 0;

    if(VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                      reinterpret_cast<LPVOID*>(&translations), &transBytes))
    {
        int count = transBytes / sizeof(LANGANDCODEPAGE);
        for(int i = 0; i < count; ++i)
        {
            QString subBlock = QString("\\StringFileInfo\\%1%2\\ProductVersion")
                                   .arg(translations[i].language, 4, 16, QChar('0'))
                                   .arg(translations[i].codePage, 4, 16, QChar('0'));

            LPVOID value = nullptr;
            UINT len = 0;
            if(VerQueryValueW(buffer.data(), subBlock.toStdWString().c_str(), &value, &len) && len > 0)
            {
                QString versionStr = QString::fromWCharArray(static_cast<const wchar_t*>(value));
                int suffixIndex = 0;
                auto ver = QVersionNumber::fromString(versionStr, &suffixIndex);
                if(!ver.isNull())
                    return ver;
            }
        }
    }

    return std::nullopt;
}

QList<LockedProcess> findLockingProcesses(const QStringList& absolutePaths)
{
    QList<LockedProcess> result;
    if(absolutePaths.isEmpty())
        return result;

    DWORD sessionHandle = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = {};
    if(RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS)
        return result;

    QVector<std::wstring> wPaths;
    wPaths.reserve(absolutePaths.size());
    for(const auto& p : absolutePaths)
        wPaths.append(p.toStdWString());

    QVector<LPCWSTR> wPathPtrs;
    wPathPtrs.reserve(wPaths.size());
    for(const auto& wp : wPaths)
        wPathPtrs.append(wp.c_str());

    if(RmRegisterResources(sessionHandle, static_cast<UINT>(wPathPtrs.size()),
                           wPathPtrs.data(), 0, nullptr, 0, nullptr) != ERROR_SUCCESS)
    {
        RmEndSession(sessionHandle);
        return result;
    }

    UINT procInfoNeeded = 0;
    UINT procInfoCount = 0;
    DWORD rebootReasons = RmRebootReasonNone;

    DWORD err = RmGetList(sessionHandle, &procInfoNeeded, &procInfoCount, nullptr, &rebootReasons);
    if(err == ERROR_MORE_DATA && procInfoNeeded > 0)
    {
        QVector<RM_PROCESS_INFO> procInfo(static_cast<int>(procInfoNeeded));
        procInfoCount = procInfoNeeded;
        err = RmGetList(sessionHandle, &procInfoNeeded, &procInfoCount,
                        procInfo.data(), &rebootReasons);
        if(err == ERROR_SUCCESS)
        {
            for(UINT i = 0; i < procInfoCount; ++i)
            {
                LockedProcess lp;
                lp.pid = procInfo[i].Process.dwProcessId;
                lp.name = QString::fromWCharArray(procInfo[i].strAppName);
                result.append(lp);
            }
        }
    }

    RmEndSession(sessionHandle);
    return result;
}

bool killProcess(quint64 pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if(!hProcess)
        return false;

    BOOL ok = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return ok != 0;
}

bool isFileLockError()
{
    DWORD err = GetLastError();
    return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION;
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

bool setExecutablePermission(const QString&)
{
    return true;
}

} // namespace Platform
