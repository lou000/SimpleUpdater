# Migrating to the New SimpleUpdater

This guide covers how to ship the transition release so existing users update seamlessly, and what to change in your main application going forward.

---

## Overview of Changes

| Area | Old | New |
|---|---|---|
| Manifest format | `updateInfo.ini` (QSettings INI) | `manifest.json` (JSON) |
| CLI interface | Flat flags (`-u`, `-s`, `-g`) | Subcommands (`generate`, `update`, `install`) |
| Version source | Manually specified via `--app_version` | Auto-detected from exe version resources |
| Force update | `force_update=true` in INI | `min_version` field in manifest or `--force` flag |
| File hashes | QSettings-encoded | SHA-256, base64-encoded |
| Source types | Local/UNC path only | Local, UNC, or HTTP/HTTPS URL |

---

## Seamless Transition Strategy

The first release using the new updater must work for users who still have the old app and old updater installed. Here's how the transition flows without any user intervention:

### What happens on the user's machine

```
1. Old app calls checkAndLaunchUpdate()
2. Old app reads updateInfo.ini from source  ──►  needs a stub INI at source
3. Old app detects version bump
4. Old app copies new updater from source over the local one
5. Old app launches new updater with old flags: -u -s <path>
6. New updater recognizes -u as legacy compat  ──►  treats it as "update" subcommand
7. New updater sees no manifest.json in target ──►  full copy of all files
8. User now has: new app + new updater + manifest.json
9. All future updates use the new system exclusively
```

### What you need to do for the transition release

**1. Generate the new manifest normally:**

```
SimpleUpdater generate %RELEASE_DIR% --app_exe MyApp.exe
```

**2. Drop a stub `updateInfo.ini` next to it** so the old app detects the version:

```ini
[SETTINGS]
app_version=2.0.0
app_exe=MyApp.exe
```

Only the `[SETTINGS]` section with `app_version` matters. The old app reads this for version comparison. The new updater ignores it entirely.

**3. Include the new `SimpleUpdater.exe` in the release directory** so the old app copies it over the local one.

That's it. The stub INI is only needed for this one transition release. All subsequent releases only need `manifest.json`.

### What the new updater handles automatically

- **Legacy CLI flags:** `-u` and `--update` at the command line are recognized and treated as the `update` subcommand. The old app's `-u -s <path>` invocation works because `-s` is already a short alias for `--source`.
- **No target manifest needed:** The updater hashes the target directory at runtime and reads the version from the application executable. No `manifest.json` is required in the target. When the target directory is empty or missing, the updater performs a full copy from source.
- **Stale file cleanup:** The old `updateInfo.ini` in the target directory is removed automatically after the update, since it's not in the source manifest. Any leftover `manifest.json` from previous updater versions is also cleaned up as stale.

---

## Updating Your Application Code

### Before (old `checkAndLaunchUpdate`)

```cpp
bool Application::checkAndLaunchUpdate()
{
#ifndef Q_OS_WIN
    return false;
#endif

    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("UPDATE");

    if (!settings.value("enable").toBool())
        return false;

    QString locStr = settings.value("location").toString();
    QDir location = locStr;

    locStr.replace("/", "\\");
    QString user = settings.value("user").toString();
    user.replace("/", "\\");
    QString pwd = settings.value("pwd").toString();

#ifdef Q_OS_WIN
    if (!user.isEmpty()) {
        WinUtils::storeNetworkCredentials(locStr, user, pwd);
    }
#endif

    QFileInfo updater(settings.value("updater").toString());
    bool updaterPresent = updater.exists() && updater.isExecutable();
    bool locationOK = location.exists() && location.isReadable() && location != QDir();

    if (!updaterPresent || !locationOK)
        return false;

    QSettings updateInfo(location.absoluteFilePath("updateInfo.ini"), QSettings::IniFormat);
    QSettings currentInfo("updateInfo.ini", QSettings::IniFormat);
    QVersionNumber updateVersion = QVersionNumber::fromString(
        updateInfo.value("SETTINGS/app_version").toString());
    QVersionNumber currentVersion = QVersionNumber::fromString(
        currentInfo.value("SETTINGS/app_version").toString());

    bool versionsValid = !updateVersion.isNull() && !updateVersion.segments().isEmpty()
                         && !currentVersion.isNull() && !currentVersion.segments().isEmpty();

    if (!versionsValid || updateVersion <= currentVersion)
        return false;

    // Copy updater in case we have a new one
    QString newUpdater = location.absoluteFilePath(settings.value("updater").toString());
    bool success = false;
    if (QFile::exists(newUpdater)) {
        QString temp = updater.absoluteFilePath() + ".bak";
        if (QFile::copy(newUpdater, temp)) {
            if (updater.exists())
                QFile::remove(updater.absoluteFilePath());
            if (QFile::rename(temp, updater.absoluteFilePath()))
                success = true;
        }
    }

    if (QProcess::startDetached(updater.absoluteFilePath(), {"-u", "-s", location.absolutePath()}))
        return true;

    return false;
}
```

### After (new `checkAndLaunchUpdate`)

```cpp
bool Application::checkAndLaunchUpdate()
{
#ifndef Q_OS_WIN
    return false;
#endif

    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.beginGroup("UPDATE");

    if (!settings.value("enable").toBool())
        return false;

    QString locStr = settings.value("location").toString();
    QDir location(locStr);

    locStr.replace("/", "\\");
    QString user = settings.value("user").toString();
    user.replace("/", "\\");
    QString pwd = settings.value("pwd").toString();

#ifdef Q_OS_WIN
    if (!user.isEmpty()) {
        WinUtils::storeNetworkCredentials(locStr, user, pwd);
    }
#endif

    QFileInfo updater(settings.value("updater").toString());
    bool updaterPresent = updater.exists() && updater.isExecutable();
    bool locationOK = location.exists() && location.isReadable() && location != QDir();

    qDebug() << "Updater:" << updater;
    qDebug() << "Update location:" << location.absolutePath();
    qDebug() << "updaterPresent =" << updaterPresent << ", locationOK =" << locationOK;

    if (!updaterPresent || !locationOK)
        return false;

    // Read remote version from source manifest.json
    QVersionNumber updateVersion;
    {
        QFile f(location.absoluteFilePath("manifest.json"));
        if (!f.open(QFile::ReadOnly))
            return false;
        auto doc = QJsonDocument::fromJson(f.readAll());
        updateVersion = QVersionNumber::fromString(doc.object()["version"].toString());
    }

    // Local version comes from the app itself, not from a file
    QVersionNumber currentVersion = QVersionNumber::fromString(
        QCoreApplication::applicationVersion());

    bool versionsValid = !updateVersion.isNull() && !updateVersion.segments().isEmpty()
                         && !currentVersion.isNull() && !currentVersion.segments().isEmpty();

    qDebug() << "Application version:" << currentVersion
             << ", Update version:" << updateVersion << versionsValid
             << (updateVersion > currentVersion);

    if (!versionsValid || updateVersion <= currentVersion)
        return false;

    // No manual updater copy -- the new updater handles self-update internally.

    QString appDir = QCoreApplication::applicationDirPath();
    if (QProcess::startDetached(updater.absoluteFilePath(),
            {"update", "--source", location.absolutePath(), "--target", appDir},
            appDir))
        return true;

    return false;
}
```

### What changed

1. **Version comparison uses `manifest.json` for the remote version and `QCoreApplication::applicationVersion()` for the local version.** The app knows its own version -- no need to read it from a file. Only the remote/source version is read from `manifest.json`. Add `#include <QJsonDocument>` and `#include <QJsonObject>`.

2. **Manual updater copy removed.** The entire `.bak` rename dance is gone. The new updater detects if its own binary needs updating by comparing hashes against the source manifest, then handles the rename/copy/relaunch cycle internally.

3. **Launch arguments changed.** `-u -s <path>` becomes `update --source <path> --target <appDir>`. The third argument to `startDetached` sets the working directory.

---

## Embed Version Resources in Your Executable

The new updater reads the version directly from your application executable. You no longer pass `--app_version` manually.

**Windows:** Your application must have a `VERSIONINFO` resource. If you use CMake, add a `version.rc` or `version.rc.in` file:

```rc
VS_VERSION_INFO VERSIONINFO
FILEVERSION     1,0,0,0
PRODUCTVERSION  1,0,0,0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "ProductVersion", "1.0.0\0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
```

**Linux:** Your application must respond to `--version` on stdout with a version string matching the pattern `X.Y.Z` (e.g. `MyApp 1.2.3`). The updater runs `<exe> --version` and parses the output.

---

## Build Pipeline Changes

### Generating manifests

**Old:**
```bash
SimpleUpdater -g --app_version %VERSION% --app_exe MyApp.exe --source %RELEASE_DIR%
```

**New:**
```bash
SimpleUpdater generate %RELEASE_DIR% --app_exe MyApp.exe
```

**With forced minimum version:**
```bash
SimpleUpdater generate %RELEASE_DIR% --app_exe MyApp.exe --min_version 1.1.0
```

### Manifest format

```json
{
    "version": "1.2.3",
    "min_version": "1.1.0",
    "app_exe": "MyApp.exe",
    "files": {
        "MyApp.exe": "base64-sha256-hash...",
        "lib/core.dll": "base64-sha256-hash...",
        "assets/logo.png": "base64-sha256-hash..."
    }
}
```

- `min_version` is omitted entirely if not specified.
- `manifest.json` itself is excluded from the `files` map.
- Paths use forward slashes and are relative to the directory root.

### Transition release checklist

For the one release that bridges old-to-new:

- [ ] `manifest.json` generated via `SimpleUpdater generate`
- [ ] Stub `updateInfo.ini` with matching `app_version` under `[SETTINGS]`
- [ ] New `SimpleUpdater.exe` included in the release directory
- [ ] New `MyApp.exe` with updated `checkAndLaunchUpdate()` and version resources

For all subsequent releases:

- [ ] `manifest.json` only (no `updateInfo.ini`)

---

## CLI Reference

### Generate

```bash
SimpleUpdater generate <directory> --app_exe <exe> [--min_version X.Y.Z]
```

### Update

```bash
# Local path
SimpleUpdater update --source C:\updates\v2 --target "C:\Program Files\MyApp"

# Network share
SimpleUpdater update --source \\server\share\v2 --target "C:\Program Files\MyApp"

# URL (downloads and extracts .zip automatically)
SimpleUpdater update --source https://releases.example.com/v2.zip --target "C:\Program Files\MyApp"

# Force update (user cannot skip)
SimpleUpdater update --source <path> --target <path> --force
```

`--target` defaults to the updater's own directory if omitted.

### Install

```bash
SimpleUpdater install [--source <path>] [--target <path>]
```

Creates a desktop shortcut automatically.

### No arguments

```bash
SimpleUpdater
```

Defaults to install mode using the updater's own directory as source.

### Legacy compat (transition only)

```bash
# Old invocation still works:
SimpleUpdater -u -s <path>
SimpleUpdater --update -s <path>
```

Treated as `update --source <path>`. Intended only for the transition from old app to new updater.

---

## Post-Update Launch Arguments

After a successful update or install, the updater launches your application with:

| Argument | Meaning |
|---|---|
| `--update` | Update completed successfully |
| `--installation` | Fresh install completed successfully |

Unchanged from the old updater.

---

## Directory Layout

The updater should live inside your application's install directory:

```
C:\Program Files\MyApp\
  MyApp.exe
  SimpleUpdater.exe
  lib/
    core.dll
  assets/
    logo.png
```

Note: the target directory does **not** contain a `manifest.json`. The updater hashes the target directory at runtime and reads the version directly from the application executable. Only the source (release) directory needs a `manifest.json`.

This enables self-update: when the source contains a newer `SimpleUpdater.exe`, the updater renames itself, copies the new version, and relaunches automatically.

---

## Appendix: Serving Updates via Nginx (LAN Only)

This section covers hosting update zip files and a version manifest on a local network using nginx, while rejecting external traffic even if port 80/443 is forwarded from the router.

### Goals

- Serve `manifest.json` at a known URL so the app can check for updates remotely.
- Serve the update `.zip` so the updater can download it via `--source https://...`.
- Restrict access to LAN clients only, even if the HTTP port is exposed to the internet via port forwarding.

### Directory structure on the server

```
/srv/updates/
  manifest.json          <-- latest version info (lightweight, checked by the app)
  releases/
    v1.0.0.zip
    v1.1.0.zip
    v2.0.0.zip           <-- latest release zip
```

`manifest.json` here is a small file the app fetches to check the available version. It can be the same `manifest.json` from the release, or a trimmed-down copy with just the version:

```json
{
    "version": "2.0.0",
    "min_version": "1.1.0",
    "app_exe": "MyApp.exe",
    "download_url": "/releases/v2.0.0.zip"
}
```

The `download_url` field is not used by the updater itself -- it's for your app to know which zip to pass to `--source`. You can also hardcode a naming convention and skip this field.

### Nginx configuration

```nginx
server {
    listen 80;
    server_name updates.local;

    root /srv/updates;

    # --- LAN-only access ---
    # Allow private network ranges
    allow 10.0.0.0/8;
    allow 172.16.0.0/12;
    allow 192.168.0.0/16;
    allow 127.0.0.0/8;

    # Deny everything else (blocks external traffic hitting via port forward)
    deny all;

    # Version check endpoint (lightweight)
    location = /manifest.json {
        default_type application/json;
        add_header Cache-Control "no-cache, must-revalidate";
    }

    # Release zip downloads
    location /releases/ {
        autoindex off;
        types {
            application/zip zip;
        }
    }

    # Block everything else
    location / {
        return 404;
    }
}
```

#### Key points

- `allow`/`deny` directives use the client's source IP. Even if your router forwards port 80 to this machine, external clients arrive with their public IP, which won't match any `allow` rule and gets a `403 Forbidden`.
- `Cache-Control: no-cache` on `manifest.json` ensures the app always gets the latest version info without stale caches.
- `autoindex off` on `/releases/` prevents directory listing.

### HTTPS variant

If your LAN clients should use HTTPS (e.g. to avoid proxy interference), use a self-signed certificate or a local CA:

```bash
# Generate a self-signed cert
openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout /etc/nginx/ssl/updates.key \
    -out /etc/nginx/ssl/updates.crt \
    -subj "/CN=updates.local"
```

```nginx
server {
    listen 443 ssl;
    server_name updates.local;

    ssl_certificate     /etc/nginx/ssl/updates.crt;
    ssl_certificate_key /etc/nginx/ssl/updates.key;

    root /srv/updates;

    allow 10.0.0.0/8;
    allow 172.16.0.0/12;
    allow 192.168.0.0/16;
    allow 127.0.0.0/8;
    deny all;

    location = /manifest.json {
        default_type application/json;
        add_header Cache-Control "no-cache, must-revalidate";
    }

    location /releases/ {
        autoindex off;
        types {
            application/zip zip;
        }
    }

    location / {
        return 404;
    }
}

# Redirect HTTP to HTTPS (still LAN-only)
server {
    listen 80;
    server_name updates.local;

    allow 10.0.0.0/8;
    allow 172.16.0.0/12;
    allow 192.168.0.0/16;
    allow 127.0.0.0/8;
    deny all;

    return 301 https://$host$request_uri;
}
```

If using a self-signed cert, Qt's `QNetworkAccessManager` will reject it by default. You have two options:

1. **Install the CA on client machines** (Group Policy on Windows, `update-ca-certificates` on Linux).
2. **Disable SSL verification in the download handler** for this specific host (not recommended for production, but acceptable on a trusted LAN).

### DNS setup

Add `updates.local` to your LAN DNS server, or add it to each client's hosts file:

```
# Windows: C:\Windows\System32\drivers\etc\hosts
# Linux:   /etc/hosts
192.168.1.50    updates.local
```

Replace `192.168.1.50` with the IP of the nginx server.

### Publishing a new release

```bash
# On the build machine / CI:
cd /path/to/release/v2.0.0

# Generate the manifest
SimpleUpdater generate . --app_exe MyApp.exe --min_version 1.1.0

# Zip the release
zip -r /srv/updates/releases/v2.0.0.zip .

# Copy manifest.json to the served location
cp manifest.json /srv/updates/manifest.json
```

### How the app uses it

The app fetches `http://updates.local/manifest.json` to check the version, then launches the updater with the zip URL:

```cpp
bool Application::checkAndLaunchUpdate()
{
    // Fetch version info from LAN server
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(QNetworkRequest(QUrl("http://updates.local/manifest.json")));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "Update check failed:" << reply->errorString();
        reply->deleteLater();
        return false;
    }

    auto doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    QVersionNumber remoteVersion = QVersionNumber::fromString(
        doc.object()["version"].toString());
    QVersionNumber localVersion = QVersionNumber::fromString(
        QCoreApplication::applicationVersion());

    if (remoteVersion.isNull() || localVersion.isNull() || remoteVersion <= localVersion)
        return false;

    // Build the download URL
    QString downloadUrl = doc.object()["download_url"].toString();
    if (downloadUrl.isEmpty())
        return false;
    if (!downloadUrl.startsWith("http"))
        downloadUrl = "http://updates.local" + downloadUrl;

    QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo updater(appDir + "/SimpleUpdater.exe");
    if (!updater.exists())
        return false;

    return QProcess::startDetached(updater.absoluteFilePath(),
        {"update", "--source", downloadUrl, "--target", appDir},
        appDir);
}
```
