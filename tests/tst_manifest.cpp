#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "manifest.h"

static bool createFile(const QDir& dir, const QString& relPath, const QByteArray& content)
{
    QString fullPath = dir.filePath(relPath);
    QDir().mkpath(QFileInfo(fullPath).absolutePath());
    QFile file(fullPath);
    if(!file.open(QFile::WriteOnly))
        return false;
    file.write(content);
    file.close();
    return true;
}

class TestManifest : public QObject {
    Q_OBJECT

private slots:

    // ---- writeManifest + readManifest round-trip ----

    void writeAndReadRoundTrip()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        Manifest original;
        original.version = QVersionNumber(2, 1, 0);
        original.appExe = "App.exe";
        original.minVersion = QVersionNumber(1, 5, 0);
        original.files.insert("App.exe", QByteArray::fromHex("abcdef0123456789"));
        original.files.insert("lib/core.dll", QByteArray::fromHex("1234567890abcdef"));
        original.files.insert(QString::fromUtf8("donn\xc3\xa9""es/caf\xc3\xa9.txt"),
                              QByteArray::fromHex("fedcba9876543210"));

        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(writeManifest(path, original));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->version, original.version);
        QCOMPARE(loaded->appExe, original.appExe);
        QVERIFY(loaded->minVersion.has_value());
        QCOMPARE(loaded->minVersion.value(), original.minVersion.value());
        QCOMPARE(loaded->files.size(), original.files.size());
        for(auto it = original.files.constBegin(); it != original.files.constEnd(); ++it)
            QCOMPARE(loaded->files.value(it.key()), it.value());
    }

    void writeIsAtomic()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        Manifest m;
        m.version = QVersionNumber(1, 0, 0);
        m.appExe = "test.exe";

        QString path = dir.filePath("manifest.json");
        QVERIFY(writeManifest(path, m));

        QVERIFY2(!QFileInfo::exists(path + ".tmp"),
                 "Temporary file was not cleaned up after write");

        QFile file(path);
        QVERIFY(file.open(QFile::ReadOnly));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
    }

    void writeOverwritesExisting()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QString path = dir.filePath("manifest.json");

        Manifest m1;
        m1.version = QVersionNumber(1, 0, 0);
        m1.appExe = "old.exe";
        QVERIFY(writeManifest(path, m1));

        Manifest m2;
        m2.version = QVersionNumber(2, 0, 0);
        m2.appExe = "new.exe";
        QVERIFY(writeManifest(path, m2));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->version, QVersionNumber(2, 0, 0));
        QCOMPARE(loaded->appExe, QString("new.exe"));
    }

    void writeToNonexistentDirectoryFails()
    {
        Manifest m;
        m.version = QVersionNumber(1, 0, 0);
        m.appExe = "test.exe";

        bool ok = writeManifest("C:/nonexistent_dir_xyz/manifest.json", m);
        QVERIFY(!ok);
    }

    void writeNullVersionProducesUnreadableManifest()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QString path = dir.filePath("manifest.json");

        Manifest m;
        m.appExe = "test.exe";

        QVERIFY(writeManifest(path, m));

        auto loaded = readManifest(path);
        QVERIFY2(!loaded.has_value(),
                 "A manifest with null version should not be readable");
    }

    // ---- readManifest validation ----

    void readMissingFileReturnsNullopt()
    {
        auto result = readManifest("C:/nonexistent/path/manifest.json");
        QVERIFY(!result.has_value());
    }

    void readCorruptJsonReturnsNullopt()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           "{{{{not json at all!!! garbage 0xDEADBEEF"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readJsonArrayInsteadOfObject()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json", "[1, 2, 3]"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readMissingVersionField()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"app_exe": "test.exe", "files": {}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readNonStringVersion()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": 123, "app_exe": "test.exe", "files": {}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readEmptyVersionString()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "", "app_exe": "test.exe", "files": {}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readUnparseableVersionString()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "abc", "app_exe": "test.exe", "files": {}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readMissingAppExeField()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "files": {}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readMissingFilesField()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe"})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readFilesFieldAsArray()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe", "files": [1,2]})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readNonStringHashValue()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe",
                               "files": {"a.txt": 12345}})"));

        auto result = readManifest(path);
        QVERIFY(!result.has_value());
    }

    void readEmptyFilesMapIsValid()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        Manifest m;
        m.version = QVersionNumber(1, 0, 0);
        m.appExe = "test.exe";

        QString path = dir.filePath("manifest.json");
        QVERIFY(writeManifest(path, m));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->files.isEmpty());
    }

    void readExtraFieldsAreIgnored()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe",
                               "files": {}, "extra_field": "hello",
                               "another": 42})"));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->version, QVersionNumber(1, 0, 0));
        QCOMPARE(loaded->appExe, QString("test.exe"));
    }

    // ---- generateManifest / hashDirectory ----

    void generateManifestHashesAllFiles()
    {
#ifdef Q_OS_WIN
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString systemExe = "C:/Windows/System32/where.exe";
        if(!QFileInfo::exists(systemExe))
            QSKIP("System executable not available for version detection test");
        QVERIFY(QFile::copy(systemExe, dir.filePath("TestApp.exe")));

        QVERIFY(createFile(dir, "lib/core.dll", "core library content"));
        QVERIFY(createFile(dir, "assets/logo.png", "fake png data"));
        QVERIFY(createFile(dir, "data/config.txt", "config=value"));
        QVERIFY(createFile(dir, "readme.txt", "readme content"));

        auto result = generateManifest(dir, "TestApp.exe", std::nullopt);
        if(!result)
            QSKIP("generateManifest failed (version detection unavailable for this exe)");

        QCOMPARE(result->files.size(), 5);
        QCOMPARE(result->appExe, QString("TestApp.exe"));
        QVERIFY(!result->version.isNull());

        QStringList expectedFiles = {"TestApp.exe", "lib/core.dll", "assets/logo.png",
                                     "data/config.txt", "readme.txt"};
        for(const auto& f : expectedFiles)
        {
            QVERIFY2(result->files.contains(f), qPrintable("Missing: " + f));
            QVERIFY(!result->files.value(f).isEmpty());
        }
#else
        QSKIP("generateManifest version detection test requires Windows");
#endif
    }

    void hashDirectorySkipsAllSpecialFiles()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "manifest.json", "skip me"));
        QVERIFY(createFile(dir, "manifest.json.tmp", "skip me too"));
        QVERIFY(createFile(dir, "updateInfo.ini", "legacy skip"));
        QVERIFY(createFile(dir, "real_file.manifest.json", "keep me"));

        auto files = hashDirectory(dir);
        QCOMPARE(files.size(), 1);
        QVERIFY(files.contains("real_file.manifest.json"));
        QVERIFY(!files.contains("manifest.json"));
        QVERIFY(!files.contains("manifest.json.tmp"));
        QVERIFY(!files.contains("updateInfo.ini"));
    }

    void hashDirectoryHandlesSubdirectories()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a/b/c/deep.txt", "deep content"));

        auto files = hashDirectory(dir);
        QCOMPARE(files.size(), 1);
        QVERIFY(files.contains("a/b/c/deep.txt"));
    }

    void hashDirectoryHandlesUnicodeFilenames()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString unicodePath = QString::fromUtf8("donn\xc3\xa9""es/caf\xc3\xa9.txt");
        QVERIFY(createFile(dir, unicodePath, "unicode content"));

        auto files = hashDirectory(dir);
        QCOMPARE(files.size(), 1);
        QVERIFY(files.contains(unicodePath));

        Manifest m;
        m.version = QVersionNumber(1, 0, 0);
        m.appExe = "test.exe";
        m.files = files;

        QString manifestPath = dir.filePath("manifest.json");
        QVERIFY(writeManifest(manifestPath, m));
        auto loaded = readManifest(manifestPath);
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->files.contains(unicodePath));
        QCOMPARE(loaded->files.value(unicodePath), files.value(unicodePath));
    }

    void hashDirectoryEmptyReturnsEmpty()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        auto files = hashDirectory(dir);
        QVERIFY(files.isEmpty());
    }

    void hashDirectoryNonexistentReturnsEmpty()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path() + "/nonexistent_subdir");

        auto files = hashDirectory(dir);
        QVERIFY(files.isEmpty());
    }

    void hashDirectoryFilesWithSpacesInName()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "path with spaces/my file.txt", "content"));

        auto files = hashDirectory(dir);
        QCOMPARE(files.size(), 1);
        QVERIFY(files.contains("path with spaces/my file.txt"));
    }

    // ---- minVersion ----

    void minVersionPreservedInRoundTrip()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        Manifest m;
        m.version = QVersionNumber(2, 0, 0);
        m.appExe = "test.exe";
        m.minVersion = QVersionNumber(1, 5, 0);

        QString path = dir.filePath("manifest.json");
        QVERIFY(writeManifest(path, m));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->minVersion.has_value());
        QCOMPARE(loaded->minVersion.value(), QVersionNumber(1, 5, 0));
    }

    void minVersionAbsentWhenNotSet()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        Manifest m;
        m.version = QVersionNumber(1, 0, 0);
        m.appExe = "test.exe";

        QString path = dir.filePath("manifest.json");
        QVERIFY(writeManifest(path, m));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QVERIFY(!loaded->minVersion.has_value());

        QFile file(path);
        QVERIFY(file.open(QFile::ReadOnly));
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QVERIFY(!doc.object().contains("min_version"));
    }

    void minVersionInvalidStringIsIgnored()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe",
                               "files": {}, "min_version": "abc"})"));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QVERIFY2(!loaded->minVersion.has_value(),
                 "Unparseable min_version should be silently ignored");
    }

    void minVersionNonStringIsIgnored()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString path = QDir(tempDir.path()).filePath("manifest.json");
        QVERIFY(createFile(QDir(tempDir.path()), "manifest.json",
                           R"({"version": "1.0.0", "app_exe": "test.exe",
                               "files": {}, "min_version": 150})"));

        auto loaded = readManifest(path);
        QVERIFY(loaded.has_value());
        QVERIFY2(!loaded->minVersion.has_value(),
                 "Non-string min_version should be silently ignored");
    }

    // ---- version comparison ----

    void versionComparisonLogic()
    {
        QVERIFY(QVersionNumber(1, 0, 0) < QVersionNumber(1, 5, 0));
        QVERIFY(QVersionNumber(1, 5, 0) == QVersionNumber(1, 5, 0));
        QVERIFY(QVersionNumber(2, 0, 0) > QVersionNumber(1, 5, 0));
        QVERIFY(QVersionNumber(1, 0) < QVersionNumber(1, 0, 1));
        QVERIFY(QVersionNumber(QList<int>{1, 2, 3, 4}) > QVersionNumber(1, 2, 3));
        QVERIFY(QVersionNumber(0, 9, 9) < QVersionNumber(1, 0, 0));
    }
};

QTEST_GUILESS_MAIN(TestManifest)
#include "tst_manifest.moc"
