#ifndef MANIFEST_H
#define MANIFEST_H

#include <QDir>
#include <QHash>
#include <QString>
#include <QVersionNumber>
#include <optional>

struct Manifest {
    QVersionNumber version;
    std::optional<QVersionNumber> minVersion;
    QString appExe;
    QHash<QString, QByteArray> files;  // relativePath -> sha256 hash (raw bytes)
};

// Read manifest from manifest.json. Returns nullopt on failure, logs reason.
std::optional<Manifest> readManifest(const QString& jsonPath);

// Write manifest atomically (write to .tmp, rename).
bool writeManifest(const QString& jsonPath, const Manifest& manifest);

// Generate manifest by scanning directory. Hashes all files, auto-detects version from appExe.
// Returns nullopt on any failure (file unreadable, version undetectable).
std::optional<Manifest> generateManifest(const QDir& directory, const QString& appExe,
                                         const std::optional<QVersionNumber>& minVersion);

// Scan a directory and hash all files, returning relativePath -> sha256 map.
// Skips manifest.json, manifest.json.tmp, updateInfo.ini, and symlinks.
QHash<QString, QByteArray> hashDirectory(const QDir& directory);

#endif // MANIFEST_H
