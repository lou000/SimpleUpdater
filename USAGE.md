# SimpleUpdater — Command-line usage

## Overview

- **No arguments** or **install** (no options): Install mode; source = updater’s directory, user picks target in UI.
- **generate**: Create `manifest.json` for a release directory.
- **update**: Update an installed app from a local path or URL.
- **install**: Install from a source directory (optional source/target; otherwise UI).

---

## Generate manifest

Create `manifest.json` in the given directory (or current directory). The executable must have embedded version info (e.g. VERSIONINFO on Windows).

```cmd
SimpleUpdater.exe generate --app_exe MyApp.exe [directory]
SimpleUpdater.exe generate --app_exe MyApp.exe C:\releases\v1.2.0
```

Optional:

- **--min_version** — Minimum version required; if the target app is older, the update is forced (mandatory).
  ```cmd
  SimpleUpdater.exe generate --app_exe MyApp.exe --min_version 1.0.0 C:\releases\v1.2.0
  ```

---

## Update

Update the target installation from a **source** (local path, UNC path, or URL to a zip). Target defaults to the updater’s own directory.

```cmd
REM Local or UNC source
SimpleUpdater.exe update --source C:\releases\v1.2.0
SimpleUpdater.exe update -s \\server\releases\v1.2.0

REM URL (zip is downloaded and extracted)
SimpleUpdater.exe update --source https://example.com/updates/MyApp-1.2.0.zip

REM Custom target directory
SimpleUpdater.exe update --source C:\releases\v1.2.0 --target "C:\Program Files\MyApp"
SimpleUpdater.exe update -s C:\releases\v1.2.0 -t D:\MyApp

REM Force update (user cannot skip)
SimpleUpdater.exe update --source https://example.com/updates/MyApp.zip --force
```

Options:

| Option | Short | Description |
|--------|--------|-------------|
| **--source** | **-s** | Source: local path, UNC path, or URL to a zip. Required. |
| **--target** | **-t** | Target directory to update. Default: updater’s directory. |
| **--force** | — | Force update (mandatory; user cannot decline). |
| **--continue-update** | — | Internal: continue after self-update relaunch. |

Legacy: **-u** or **--update** are accepted as the “update” subcommand.

---

## Install

Install the application from a source directory. If **--source** or **--target** are omitted, the UI is shown (source may default to updater’s directory, user chooses target).

```cmd
REM Full install from source to target (no UI for paths)
SimpleUpdater.exe install --source C:\releases\v1.2.0 --target "C:\Program Files\MyApp"
SimpleUpdater.exe install -s C:\releases\v1.2.0 -t D:\MyApp

REM UI: user picks target, source = updater directory
SimpleUpdater.exe
SimpleUpdater.exe install
```

Options:

| Option | Short | Description |
|--------|--------|-------------|
| **--source** | **-s** | Source directory. Optional; if omitted, uses updater’s directory. |
| **--target** | **-t** | Target directory. Optional; if omitted, user chooses in UI. |

---

## Source layout (update/install)

- **Local/UNC source**: A directory that contains at least **manifest.json** (and the files listed in it). Generate it with the **generate** subcommand.
- **URL source**: A zip URL. The zip must contain **manifest.json** at the root or in a single top-level folder; that folder is used as the source after extraction.
