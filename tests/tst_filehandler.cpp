#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "filehandler.h"
#include "platform/platform.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <cerrno>
#include <unistd.h>
#endif

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

static QByteArray readFileContent(const QString& path)
{
    QFile file(path);
    if(!file.open(QFile::ReadOnly))
        return {};
    return file.readAll();
}

class TestFileHandler : public QObject {
    Q_OBJECT

private slots:

    // ---- hashFile ----

    void hashFileProducesConsistentResults()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "test.txt", "hello world"));
        QString path = dir.filePath("test.txt");

        QByteArray hash1 = FileHandler::hashFile(path);
        QByteArray hash2 = FileHandler::hashFile(path);
        QVERIFY(!hash1.isEmpty());
        QCOMPARE(hash1, hash2);

        QCryptographicHash expected(QCryptographicHash::Sha256);
        expected.addData(QByteArrayLiteral("hello world"));
        QCOMPARE(hash1, expected.result());
    }

    void hashFileEmptyFile()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "empty.txt", ""));
        QByteArray hash = FileHandler::hashFile(dir.filePath("empty.txt"));
        QVERIFY2(!hash.isEmpty(),
                 "SHA-256 of empty input should be a valid 32-byte hash, not empty");

        QCryptographicHash expected(QCryptographicHash::Sha256);
        QCOMPARE(hash, expected.result());
    }

    void hashFileNonexistentReturnsEmpty()
    {
        QByteArray hash = FileHandler::hashFile("C:/nonexistent/path/file.txt");
        QVERIFY(hash.isEmpty());
    }

    void hashFileLargeFile()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QByteArray largeData(10 * 1024 * 1024, 'A');
        QVERIFY(createFile(dir, "large.bin", largeData));

        QByteArray hash = FileHandler::hashFile(dir.filePath("large.bin"));
        QVERIFY(!hash.isEmpty());
        QCOMPARE(hash.size(), 32);
    }

    // ---- computeDiff ----

    void computeDiffIdenticalManifests()
    {
        QHash<QString, QByteArray> files;
        files.insert("a.txt", "hash_a");
        files.insert("b.txt", "hash_b");
        files.insert("c.txt", "hash_c");

        FileDiff diff = FileHandler::computeDiff(files, files);
        QVERIFY(diff.toAdd.isEmpty());
        QVERIFY(diff.toUpdate.isEmpty());
        QVERIFY(diff.toRemove.isEmpty());
        QCOMPARE(diff.unchanged.size(), 3);
    }

    void computeDiffAllNew()
    {
        QHash<QString, QByteArray> source;
        source.insert("a.txt", "hash_a");
        source.insert("b.txt", "hash_b");
        source.insert("c.txt", "hash_c");

        FileDiff diff = FileHandler::computeDiff(source, {});
        QCOMPARE(diff.toAdd.size(), 3);
        QVERIFY(diff.toUpdate.isEmpty());
        QVERIFY(diff.toRemove.isEmpty());
        QVERIFY(diff.unchanged.isEmpty());
    }

    void computeDiffAllRemoved()
    {
        QHash<QString, QByteArray> target;
        target.insert("a.txt", "hash_a");
        target.insert("b.txt", "hash_b");
        target.insert("c.txt", "hash_c");

        FileDiff diff = FileHandler::computeDiff({}, target);
        QVERIFY(diff.toAdd.isEmpty());
        QVERIFY(diff.toUpdate.isEmpty());
        QCOMPARE(diff.toRemove.size(), 3);
        QVERIFY(diff.unchanged.isEmpty());
    }

    void computeDiffMixed()
    {
        QHash<QString, QByteArray> source;
        source.insert("a.txt", "hash_a");
        source.insert("b.txt", "hash_b_new");
        source.insert("c.txt", "hash_c");

        QHash<QString, QByteArray> target;
        target.insert("a.txt", "hash_a");
        target.insert("b.txt", "hash_b_old");
        target.insert("d.txt", "hash_d");

        FileDiff diff = FileHandler::computeDiff(source, target);
        QCOMPARE(diff.unchanged.size(), 1);
        QVERIFY(diff.unchanged.contains("a.txt"));
        QCOMPARE(diff.toUpdate.size(), 1);
        QVERIFY(diff.toUpdate.contains("b.txt"));
        QCOMPARE(diff.toAdd.size(), 1);
        QVERIFY(diff.toAdd.contains("c.txt"));
        QCOMPARE(diff.toRemove.size(), 1);
        QVERIFY(diff.toRemove.contains("d.txt"));
    }

    void computeDiffBothEmpty()
    {
        FileDiff diff = FileHandler::computeDiff({}, {});
        QVERIFY(diff.toAdd.isEmpty());
        QVERIFY(diff.toUpdate.isEmpty());
        QVERIFY(diff.toRemove.isEmpty());
        QVERIFY(diff.unchanged.isEmpty());
    }

    void computeDiffAllUpdated()
    {
        QHash<QString, QByteArray> source;
        source.insert("a.txt", "hash_a_v2");
        source.insert("b.txt", "hash_b_v2");

        QHash<QString, QByteArray> target;
        target.insert("a.txt", "hash_a_v1");
        target.insert("b.txt", "hash_b_v1");

        FileDiff diff = FileHandler::computeDiff(source, target);
        QVERIFY(diff.toAdd.isEmpty());
        QCOMPARE(diff.toUpdate.size(), 2);
        QVERIFY(diff.toRemove.isEmpty());
        QVERIFY(diff.unchanged.isEmpty());
    }

    // ---- copyFiles ----

    void copyFilesBasic()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "a.txt", "aaa"));
        QVERIFY(createFile(src, "b.txt", "bbb"));
        QVERIFY(createFile(src, "c.txt", "ccc"));

        FileHandler handler;
        QVERIFY(handler.copyFiles(src, tgt, {"a.txt", "b.txt", "c.txt"}));

        QCOMPARE(readFileContent(tgt.filePath("a.txt")), QByteArray("aaa"));
        QCOMPARE(readFileContent(tgt.filePath("b.txt")), QByteArray("bbb"));
        QCOMPARE(readFileContent(tgt.filePath("c.txt")), QByteArray("ccc"));
    }

    void copyFilesCreatesSubdirectories()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "a/b/c.txt", "nested content"));

        FileHandler handler;
        QVERIFY(handler.copyFiles(src, tgt, {"a/b/c.txt"}));
        QVERIFY(QFileInfo::exists(tgt.filePath("a/b/c.txt")));
        QCOMPARE(readFileContent(tgt.filePath("a/b/c.txt")), QByteArray("nested content"));
    }

    void copyFilesPreservesContent()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QByteArray binaryContent;
        binaryContent.append('\0');
        binaryContent.append("binary", 6);
        binaryContent.append('\xFF');
        binaryContent.append('\xFE');
        binaryContent.append('\0');
        binaryContent.append("data", 4);

        QVERIFY(createFile(src, "binary.dat", binaryContent));

        FileHandler handler;
        QVERIFY(handler.copyFiles(src, tgt, {"binary.dat"}));
        QCOMPARE(readFileContent(tgt.filePath("binary.dat")), binaryContent);
    }

    void copyFilesOverwritesExisting()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "a.txt", "new content"));
        QVERIFY(createFile(tgt, "a.txt", "old content"));

        FileHandler handler;
        QVERIFY(handler.copyFiles(src, tgt, {"a.txt"}));
        QCOMPARE(readFileContent(tgt.filePath("a.txt")), QByteArray("new content"));
    }

    void copyFilesEmptyListSucceeds()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.copyFiles(src, tgt, {}));
        QCOMPARE(spy.count(), 0);
    }

    void copyFilesSkipsSelf()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "file.txt", "content"));

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.copyFiles(src, tgt, {"file.txt"}));
        QCOMPARE(spy.count(), 1);
        QString desc = spy.at(0).at(0).toString();
        QVERIFY(desc.contains("COPY"));
        QVERIFY(!desc.contains("SKIP"));
    }

    void copyFilesReportsProgress()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QStringList files;
        for(int i = 0; i < 5; ++i)
        {
            QString name = QString("file%1.txt").arg(i);
            QVERIFY(createFile(src, name, "content"));
            files.append(name);
        }

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.copyFiles(src, tgt, files));
        QCOMPARE(spy.count(), 5);
        for(int i = 0; i < 5; ++i)
            QCOMPARE(spy.at(i).at(1).toBool(), true);
    }

    void copyFilesNonexistentSourceFails()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(!handler.copyFiles(src, tgt, {"nonexistent.txt"}));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);
    }

    void copyFilesMultipleFailuresContinues()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "good.txt", "ok"));

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        bool result = handler.copyFiles(src, tgt, {"missing1.txt", "good.txt", "missing2.txt"});
        QVERIFY(!result);

        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(1).toBool(), false);
        QCOMPARE(spy.at(1).at(1).toBool(), true);
        QCOMPARE(spy.at(2).at(1).toBool(), false);

        QCOMPARE(readFileContent(tgt.filePath("good.txt")), QByteArray("ok"));
    }

    void copyFilesCancellation()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "a.txt", "aaa"));
        QVERIFY(createFile(src, "b.txt", "bbb"));

        FileHandler handler;
        handler.cancel();

        QSignalSpy cancelSpy(&handler, &FileHandler::cancelled);
        QSignalSpy progressSpy(&handler, &FileHandler::progressUpdated);

        QVERIFY(!handler.copyFiles(src, tgt, {"a.txt", "b.txt"}));

        QCOMPARE(cancelSpy.count(), 1);
        QCOMPARE(progressSpy.count(), 0);
        QVERIFY(!QFileInfo::exists(tgt.filePath("a.txt")));
    }

    void cancelResetAllowsSubsequentOperations()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "a.txt", "aaa"));

        FileHandler handler;
        QVERIFY(!handler.isCancelled());
        handler.cancel();
        QVERIFY(handler.isCancelled());
        handler.resetCancel();
        QVERIFY(!handler.isCancelled());

        QVERIFY(handler.copyFiles(src, tgt, {"a.txt"}));
        QCOMPARE(readFileContent(tgt.filePath("a.txt")), QByteArray("aaa"));
    }

    // ---- removeFiles ----

    void removeFilesBasic()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "c.txt", "ccc"));

        FileHandler handler;
        QVERIFY(handler.removeFiles(dir, {"a.txt", "b.txt", "c.txt"}));
        QVERIFY(!QFileInfo::exists(dir.filePath("a.txt")));
        QVERIFY(!QFileInfo::exists(dir.filePath("b.txt")));
        QVERIFY(!QFileInfo::exists(dir.filePath("c.txt")));
    }

    void removeFilesNonexistentIsNotError()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.removeFiles(dir, {"nonexistent.txt"}));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), true);
        QVERIFY(spy.at(0).at(0).toString().contains("already gone"));
    }

    void removeFilesEmptyListSucceeds()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.removeFiles(dir, {}));
        QCOMPARE(spy.count(), 0);
    }

    void removeFilesCleansEmptyDirs()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a/b/file.txt", "content"));

        FileHandler handler;
        QVERIFY(handler.removeFiles(dir, {"a/b/file.txt"}));
        handler.removeEmptyDirectories(dir);
        QVERIFY(!QDir(dir.filePath("a/b")).exists());
        QVERIFY(!QDir(dir.filePath("a")).exists());
    }

    // ---- renameToBackup / restoreFromBackup ----

    void renameToBackupAndRestore()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "c.txt", "ccc"));

        QStringList files = {"a.txt", "b.txt", "c.txt"};
        FileHandler handler;

        QVERIFY(handler.renameToBackup(dir, files));
        for(const auto& f : files)
        {
            QVERIFY2(!QFileInfo::exists(dir.filePath(f)),
                     qPrintable(f + " should not exist after backup"));
            QVERIFY2(QFileInfo::exists(dir.filePath(f + ".bak")),
                     qPrintable(f + ".bak should exist after backup"));
        }

        QVERIFY(handler.restoreFromBackup(dir, files));
        for(const auto& f : files)
        {
            QVERIFY(QFileInfo::exists(dir.filePath(f)));
            QVERIFY(!QFileInfo::exists(dir.filePath(f + ".bak")));
        }

        QCOMPARE(readFileContent(dir.filePath("a.txt")), QByteArray("aaa"));
        QCOMPARE(readFileContent(dir.filePath("b.txt")), QByteArray("bbb"));
        QCOMPARE(readFileContent(dir.filePath("c.txt")), QByteArray("ccc"));
    }

    void renameToBackupPartialFailureRestores()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "c.txt", "ccc"));

        // Non-empty directory at b.txt.bak blocks QFile::remove and QFile::rename
        QDir().mkpath(dir.filePath("b.txt.bak/blocker"));
        QVERIFY(createFile(QDir(dir.filePath("b.txt.bak")), "blocker/x.txt", "x"));

        QStringList files = {"a.txt", "b.txt", "c.txt"};
        FileHandler handler;

        QVERIFY(!handler.renameToBackup(dir, files));
        QVERIFY2(QFileInfo::exists(dir.filePath("a.txt")),
                 "a.txt was renamed before failure and must be rolled back");
        QVERIFY2(QFileInfo::exists(dir.filePath("b.txt")),
                 "b.txt rename failed, should still be in place");
        QVERIFY2(QFileInfo::exists(dir.filePath("c.txt")),
                 "c.txt was never reached, should still be in place");
    }

    void renameToBackupMissingFileSkipped()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "exists.txt", "data"));

        FileHandler handler;
        QSignalSpy spy(&handler, &FileHandler::progressUpdated);
        QVERIFY(handler.renameToBackup(dir, {"missing.txt", "exists.txt"}));

        QCOMPARE(spy.count(), 2);
        QVERIFY(spy.at(0).at(0).toString().contains("not found"));
        QCOMPARE(spy.at(0).at(1).toBool(), true);
        QCOMPARE(spy.at(1).at(1).toBool(), true);
        QVERIFY(QFileInfo::exists(dir.filePath("exists.txt.bak")));
    }

    void renameToBackupEmptyListSucceeds()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        FileHandler handler;
        QVERIFY(handler.renameToBackup(dir, {}));
    }

    void renameToBackupInSubdirectory()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "sub/dir/file.txt", "nested"));

        FileHandler handler;
        QVERIFY(handler.renameToBackup(dir, {"sub/dir/file.txt"}));
        QVERIFY(!QFileInfo::exists(dir.filePath("sub/dir/file.txt")));
        QVERIFY(QFileInfo::exists(dir.filePath("sub/dir/file.txt.bak")));

        QVERIFY(handler.restoreFromBackup(dir, {"sub/dir/file.txt"}));
        QVERIFY(QFileInfo::exists(dir.filePath("sub/dir/file.txt")));
        QCOMPARE(readFileContent(dir.filePath("sub/dir/file.txt")), QByteArray("nested"));
    }

    void restoreFromBackupIdempotent()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));

        FileHandler handler;
        QVERIFY(handler.restoreFromBackup(dir, {"a.txt"}));
        QVERIFY(QFileInfo::exists(dir.filePath("a.txt")));
        QCOMPARE(readFileContent(dir.filePath("a.txt")), QByteArray("aaa"));
    }

    void restoreFromBackupPartialBakFiles()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt.bak", "old_a"));
        QVERIFY(createFile(dir, "b.txt", "current_b"));

        FileHandler handler;
        QVERIFY(handler.restoreFromBackup(dir, {"a.txt", "b.txt"}));

        QVERIFY(QFileInfo::exists(dir.filePath("a.txt")));
        QCOMPARE(readFileContent(dir.filePath("a.txt")), QByteArray("old_a"));
        QVERIFY(QFileInfo::exists(dir.filePath("b.txt")));
        QCOMPARE(readFileContent(dir.filePath("b.txt")), QByteArray("current_b"));
    }

    void cleanupBackupsDeletesBakFiles()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "a.txt.bak", "old_aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "b.txt.bak", "old_bbb"));

        FileHandler handler;
        handler.cleanupBackups(dir, {"a.txt", "b.txt"});

        QVERIFY(!QFileInfo::exists(dir.filePath("a.txt.bak")));
        QVERIFY(!QFileInfo::exists(dir.filePath("b.txt.bak")));
        QVERIFY(QFileInfo::exists(dir.filePath("a.txt")));
        QVERIFY(QFileInfo::exists(dir.filePath("b.txt")));
    }

    void cleanupBackupsNonexistentBakIsSilent()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));

        FileHandler handler;
        handler.cleanupBackups(dir, {"a.txt"});
        QVERIFY(QFileInfo::exists(dir.filePath("a.txt")));
    }

    // ---- verifyFiles ----

    void verifyFilesAllMatch()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "c.txt", "ccc"));

        QHash<QString, QByteArray> expected;
        expected.insert("a.txt", FileHandler::hashFile(dir.filePath("a.txt")));
        expected.insert("b.txt", FileHandler::hashFile(dir.filePath("b.txt")));
        expected.insert("c.txt", FileHandler::hashFile(dir.filePath("c.txt")));

        FileHandler handler;
        QVERIFY(handler.verifyFiles(dir, expected).isEmpty());
    }

    void verifyFilesDetectsMismatch()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));
        QVERIFY(createFile(dir, "c.txt", "ccc"));

        QHash<QString, QByteArray> expected;
        expected.insert("a.txt", FileHandler::hashFile(dir.filePath("a.txt")));
        expected.insert("b.txt", FileHandler::hashFile(dir.filePath("b.txt")));
        expected.insert("c.txt", FileHandler::hashFile(dir.filePath("c.txt")));

        QVERIFY(createFile(dir, "b.txt", "TAMPERED"));

        FileHandler handler;
        QStringList mismatches = handler.verifyFiles(dir, expected);
        QCOMPARE(mismatches.size(), 1);
        QVERIFY(mismatches.contains("b.txt"));
    }

    void verifyFilesMissingFileReportedAsMismatch()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));

        QHash<QString, QByteArray> expected;
        expected.insert("a.txt", FileHandler::hashFile(dir.filePath("a.txt")));
        expected.insert("missing.txt", QByteArray::fromHex("abcdef0123456789"));

        FileHandler handler;
        QStringList mismatches = handler.verifyFiles(dir, expected);
        QCOMPARE(mismatches.size(), 1);
        QVERIFY(mismatches.contains("missing.txt"));
    }

    void verifyFilesEmptyMapReturnsNoMismatches()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));

        FileHandler handler;
        QStringList mismatches = handler.verifyFiles(dir, {});
        QVERIFY(mismatches.isEmpty());
    }

    void verifyFilesAllMismatch()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a.txt", "aaa"));
        QVERIFY(createFile(dir, "b.txt", "bbb"));

        QHash<QString, QByteArray> expected;
        expected.insert("a.txt", QByteArray(32, '\x00'));
        expected.insert("b.txt", QByteArray(32, '\xFF'));

        FileHandler handler;
        QStringList mismatches = handler.verifyFiles(dir, expected);
        QCOMPARE(mismatches.size(), 2);
    }

    // ---- removeEmptyDirectories ----

    void removeEmptyDirectoriesBasic()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QDir().mkpath(dir.filePath("a/b"));

        FileHandler handler;
        handler.removeEmptyDirectories(dir);
        QVERIFY(!QDir(dir.filePath("a/b")).exists());
        QVERIFY(!QDir(dir.filePath("a")).exists());
    }

    void removeEmptyDirectoriesSkipsRoot()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        FileHandler handler;
        handler.removeEmptyDirectories(dir);
        QVERIFY(dir.exists());
    }

    void removeEmptyDirectoriesKeepsNonEmpty()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "a/file.txt", "content"));
        QDir().mkpath(dir.filePath("b"));

        FileHandler handler;
        handler.removeEmptyDirectories(dir);
        QVERIFY(QDir(dir.filePath("a")).exists());
        QVERIFY(!QDir(dir.filePath("b")).exists());
    }

    void removeEmptyDirectoriesDeeplyNested()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QDir().mkpath(dir.filePath("a/b/c/d/e"));

        FileHandler handler;
        handler.removeEmptyDirectories(dir);
        QVERIFY(!QDir(dir.filePath("a")).exists());
    }

    void removeEmptyDirectoriesMixedTree()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QVERIFY(createFile(dir, "keep/file.txt", "content"));
        QDir().mkpath(dir.filePath("keep/empty_child"));
        QDir().mkpath(dir.filePath("remove_me/also_remove"));

        FileHandler handler;
        handler.removeEmptyDirectories(dir);

        QVERIFY(QDir(dir.filePath("keep")).exists());
        QVERIFY(!QDir(dir.filePath("keep/empty_child")).exists());
        QVERIFY(!QDir(dir.filePath("remove_me")).exists());
    }

#ifdef Q_OS_WIN

    // ---- Windows file locking tests ----

    void isFileLockErrorDetectsSharingViolation()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "test.txt", "content"));

        QString path = dir.filePath("test.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        QVERIFY(!QFile::remove(path));
        QVERIFY(Platform::isFileLockError());

        CloseHandle(hFile);
    }

    void removeFilesRetriesOnLockAndSucceeds()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "locked.txt", "content"));

        QString fullPath = dir.filePath("locked.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(fullPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return true;
        });

        QVERIFY(handler.removeFiles(dir, {"locked.txt"}));
        QCOMPARE(resolverCalls, 1);
        QVERIFY(!QFileInfo::exists(fullPath));
    }

    void removeFilesFailsWhenResolverReturnsFalse()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "locked.txt", "content"));

        QString fullPath = dir.filePath("locked.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(fullPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            return false;
        });

        QVERIFY(!handler.removeFiles(dir, {"locked.txt"}));
        QCOMPARE(resolverCalls, 1);
        QVERIFY(QFileInfo::exists(fullPath));

        CloseHandle(hFile);
    }

    void copyFilesRetriesOnLockedTarget()
    {
        QTemporaryDir srcTempDir, tgtTempDir;
        QVERIFY(srcTempDir.isValid());
        QVERIFY(tgtTempDir.isValid());
        QDir src(srcTempDir.path()), tgt(tgtTempDir.path());

        QVERIFY(createFile(src, "file.txt", "new content"));
        QVERIFY(createFile(tgt, "file.txt", "old content"));

        QString tgtPath = tgt.filePath("file.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(tgtPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return true;
        });

        QVERIFY(handler.copyFiles(src, tgt, {"file.txt"}));
        QCOMPARE(resolverCalls, 1);
        QCOMPARE(readFileContent(tgtPath), QByteArray("new content"));
    }

    void renameToBackupRetriesOnLock()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "file.txt", "content"));

        QString fullPath = dir.filePath("file.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(fullPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
            return true;
        });

        QVERIFY(handler.renameToBackup(dir, {"file.txt"}));
        QCOMPARE(resolverCalls, 1);
        QVERIFY(!QFileInfo::exists(fullPath));
        QVERIFY(QFileInfo::exists(fullPath + ".bak"));
    }

    void noResolverSetLockedFileFails()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "locked.txt", "content"));

        QString fullPath = dir.filePath("locked.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(fullPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        FileHandler handler;
        QVERIFY(!handler.removeFiles(dir, {"locked.txt"}));
        QVERIFY(QFileInfo::exists(fullPath));

        CloseHandle(hFile);
    }

    void lockResolverRetriesMultipleTimes()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "locked.txt", "content"));

        QString fullPath = dir.filePath("locked.txt");
        HANDLE hFile = CreateFileW(
            reinterpret_cast<LPCWSTR>(fullPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(hFile != INVALID_HANDLE_VALUE);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            if(resolverCalls == 3)
            {
                CloseHandle(hFile);
                hFile = INVALID_HANDLE_VALUE;
            }
            return true;
        });

        QVERIFY(handler.removeFiles(dir, {"locked.txt"}));
        QCOMPARE(resolverCalls, 3);
        QVERIFY(!QFileInfo::exists(fullPath));
    }

#endif // Q_OS_WIN

#ifdef Q_OS_LINUX

    // ---- Linux file locking tests ----

    void isFileLockErrorFalseForPermissionDenied()
    {
        errno = EACCES;
        QVERIFY(!Platform::isFileLockError());
    }

    void isFileLockErrorTrueForTextBusy()
    {
        errno = ETXTBSY;
        QVERIFY(Platform::isFileLockError());
    }

    void isFileLockErrorTrueForBusy()
    {
        errno = EBUSY;
        QVERIFY(Platform::isFileLockError());
    }

    void resolverNotCalledOnPermissionError()
    {
        if(geteuid() == 0)
            QSKIP("Running as root, cannot test permission-denied behavior");

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(createFile(dir, "file.txt", "content"));

        QFile::setPermissions(dir.absolutePath(),
                              QFileDevice::ReadOwner | QFileDevice::ExeOwner);

        int resolverCalls = 0;
        FileHandler handler;
        handler.setLockResolver([&](const QString&) -> bool {
            resolverCalls++;
            return false;
        });

        QVERIFY(!handler.removeFiles(dir, {"file.txt"}));
        QCOMPARE(resolverCalls, 0);

        QFile::setPermissions(dir.absolutePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

#endif // Q_OS_LINUX
};

QTEST_GUILESS_MAIN(TestFileHandler)
#include "tst_filehandler.moc"
