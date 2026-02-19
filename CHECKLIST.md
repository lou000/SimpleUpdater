# Phase 2 Tests Checklist

## T2.1 -- CLI Refactor

- [ ] `SimpleUpdater generate C:\test\source_v1 --app_exe TestApp.exe` generates `manifest.json` and exits
- [ ] `SimpleUpdater update --source C:\test\source_v2 --target C:\test\target` shows update GUI
- [ ] `SimpleUpdater install --source C:\test\source_v1` shows install GUI
- [ ] `SimpleUpdater` (no args) shows install dialog
- [ ] `SimpleUpdater blah` shows error about unknown subcommand
- [ ] `generate` on dir with exe that has version resources produces correct version in manifest
- [ ] `generate` on dir with exe that has NO version resources shows clear error, no manifest generated
- [ ] `generate` twice with same exe (no version bump) shows warning about matching version on second run
- [ ] `generate` with `--app_exe nonexistent.exe` shows error and aborts
- [ ] Target at v2.0.0, source at v1.0.0 -- update shows "Cannot downgrade" error
- [ ] Target and source both at v1.0.0 -- update shows "Already up to date"
- [ ] Missing `manifest.json` in target -- update warns and does full copy
- [ ] Corrupt `manifest.json` in target (invalid JSON) -- update warns and does full copy
- [ ] Missing `manifest.json` in source -- update shows hard error and aborts

## T2.2 -- Manifest (JSON format)

- [ ] Generated `manifest.json` is valid JSON
- [ ] `version` matches the exe
- [ ] `app_exe` is the relative path as given
- [ ] `files` contains all files in the directory with forward-slash paths
- [ ] `manifest.json` itself is NOT in the `files` map
- [ ] Hash values are base64-encoded strings
- [ ] Atomic write: killing process mid-generation leaves old `manifest.json` intact or no manifest (never half-written)
- [ ] `--min_version 1.5.0` with target at v1.0.0 -- "Update Later" button is hidden (mandatory)
- [ ] `--min_version 1.5.0` with target at v1.5.0 -- "Update Later" button IS visible
- [ ] `--min_version 1.5.0` with target at v2.0.0 -- "Already up to date" or "Update Later" visible

## T2.3 -- FileHandler

- [ ] Install from source_v1 to empty target -- every file identical (binary-compare)
- [ ] Incremental update: `core.dll` updated, `logo.png` untouched, `banner.png` added, `data\config.txt` removed, empty `data\` directory removed
- [ ] Permission preservation on Linux (executable permission preserved after copy)
- [ ] Cancel mid-update (100+ files) -- stops promptly, staging cleaned up, target untouched
- [ ] `SimpleUpdater.exe` in source is skipped during update (shown as "SKIP" in log)

## T2.4/T2.5 -- UpdateController + Staging Flow

- [ ] Happy path: update v1 to v2, all changes applied correctly, no `.bak` files or staging dirs left behind
- [ ] Rollback on locked file: locked file causes rename-to-`.bak` to fail, all `.bak` files restored, staging cleaned up, target back to pre-update state, error message names the locked file
- [ ] Verification failure: modified file in staging after stage but before apply triggers mismatch and rollback
- [ ] Post-update verification: log shows "VERIFY" entries, all OK

## T2.6 -- Stale File Cleanup

- [ ] Add `stale_file.txt` to target before update, verify it is removed after update
- [ ] Verify `manifest.json` is NOT removed

## T2.7 -- Process Lock Detection

- [ ] Lock a file in target, run update -- dialog appears listing locking process name and PID
- [ ] Release the lock, click "Retry" -- update proceeds
- [ ] Lock a file, click "Kill All" -- locking process terminated, update proceeds
- [ ] Lock a file, click "Cancel" -- updater returns to update screen without modifying anything

## T2.8 -- Self-Update

- [ ] Modified `SimpleUpdater.exe` in source: log shows self-update detected, new window appears, old `SimpleUpdater_old.exe` cleaned up, update completes
- [ ] Same `SimpleUpdater.exe` in source (same hash): no self-update occurs, update proceeds normally
- [ ] Recovery: renamed exe to `SimpleUpdater.exe_old` can be renamed back and works

## T2.9 -- Shortcut Management

- [ ] Install creates desktop shortcut with correct name (derived from `app_exe`)
- [ ] Exe name change (v1 `TestApp.exe` -> v2 `NewApp.exe`): old shortcut removed, new shortcut created
- [ ] Same exe name across versions: shortcut is kept/works

## T2.10 -- URL Download

- [ ] Local path: `--source C:\test\source_v2` works as before
- [ ] UNC network share: `--source \\server\share\source_v2` works (files copied from share)
- [ ] HTTP URL: `--source http://localhost:8080/source_v2.zip` -- download progress shown, zip extracted, update applied, temp dir cleaned up
- [ ] Bad URL: `--source http://localhost:8080/nonexistent.zip` -- clear error about download failure (404)
- [ ] Network timeout: `--source http://10.255.255.1/update.zip` -- timeout error message appears

## T2.11 -- Edge Cases

- [ ] Unicode filenames (`café.txt`, `données\日本語.txt`): manifest contains paths correctly, install/update works, diff detects changes
- [ ] Spaces in paths (`C:\test\my source v2`, `C:\test\My Application`): generate, install, update all succeed
- [ ] `generate --min_version 1.5.0` writes `"min_version": "1.5.0"` to manifest; without flag, `min_version` is absent
- [ ] Empty target directory (no manifest, no files): update treats as fresh install, full copy, writes manifest
- [ ] Read-only files in target: update handles it (clears read-only, renames to `.bak`) or reports clear error

## End-to-End Smoke Tests

- [ ] Fresh install from local source: all files copied, manifest created, shortcut created, app launches
- [ ] Incremental update from local source: correct diff applied, stale files removed, app relaunches
- [ ] Incremental update from network share
- [ ] Incremental update from URL (HTTP zip)
- [ ] Forced update (`min_version` higher than target): "Update Later" is hidden
- [ ] Skippable update (no `min_version` or below target): "Update Later" works, launches old app
- [ ] Update with process locks: lock dialog, kill, retry flow
- [ ] Update with self-update: relaunch and completion
- [ ] Update from very old version (no JSON manifest in target): full copy, new JSON manifest written
- [ ] Cancel mid-update: staging cleaned up, target completely untouched
- [ ] Unicode and spaces: generate, install, update all work with `café.txt` and paths with spaces
