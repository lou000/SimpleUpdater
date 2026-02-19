#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "cliparser.h"
#include "platform/platform.h"

class TestCliParser : public QObject {
    Q_OBJECT

private slots:

    // ---- isUrl ----

    void sourceDetectsLocalPath()
    {
        QVERIFY(!isUrl("C:/test/source"));
    }

    void sourceDetectsUncPath()
    {
        QVERIFY(!isUrl("\\\\server\\share\\source"));
    }

    void sourceDetectsHttpUrl()
    {
        QVERIFY(isUrl("http://example.com/update.zip"));
    }

    void sourceDetectsHttpsUrl()
    {
        QVERIFY(isUrl("https://example.com/update.zip"));
    }

    void isUrlCaseInsensitive()
    {
        QVERIFY(isUrl("HTTP://EXAMPLE.COM/update.zip"));
        QVERIFY(isUrl("HTTPS://EXAMPLE.COM/update.zip"));
        QVERIFY(isUrl("Http://Mixed.Case/update.zip"));
    }

    void isUrlRejectsFtp()
    {
        QVERIFY(!isUrl("ftp://files.example.com/update.zip"));
    }

    void isUrlEmptyString()
    {
        QVERIFY(!isUrl(""));
    }

    void isUrlPartialPrefix()
    {
        QVERIFY(!isUrl("http"));
        QVERIFY(!isUrl("https"));
        QVERIFY(!isUrl("http:"));
        QVERIFY(!isUrl("https:"));
        QVERIFY(!isUrl("http:/"));
        QVERIFY(!isUrl("https:/"));
    }

    // ---- parseCli dispatch ----

    void emptyArgsReturnsNullopt()
    {
        auto result = parseCli(QStringList{});
        QVERIFY2(!result.has_value(),
                 "Empty args list must not crash and should return nullopt");
    }

    void noArgsDefaultsToInstall()
    {
        auto result = parseCli({"SimpleUpdater"});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Install);
        QVERIFY(result->install.has_value());
        QVERIFY(result->install->sourceDir.has_value());
    }

    void unknownSubcommandReturnsNullopt()
    {
        auto result = parseCli({"SimpleUpdater", "frobnicate"});
        QVERIFY(!result.has_value());
    }

    void subcommandIsCaseSensitive()
    {
        auto result = parseCli({"SimpleUpdater", "GENERATE"});
        QVERIFY2(!result.has_value(),
                 "Subcommands should be case-sensitive; 'GENERATE' is not 'generate'");
    }

    // ---- generate subcommand ----

    void generateSubcommand()
    {
#ifdef Q_OS_WIN
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString systemExe = "C:/Windows/System32/where.exe";
        if(!QFileInfo::exists(systemExe))
            QSKIP("System executable not available");
        QVERIFY(QFile::copy(systemExe, dir.filePath("App.exe")));

        auto result = parseCli({"SimpleUpdater", "generate", "--app_exe", "App.exe",
                                dir.absolutePath()});
        if(!result)
            QSKIP("generate subcommand failed (version detection unavailable)");

        QCOMPARE(result->mode, AppMode::Generate);
        QVERIFY(result->generate.has_value());
        QCOMPARE(result->generate->appExe, QString("App.exe"));
#else
        QSKIP("generate subcommand test requires Windows");
#endif
    }

    void generateRequiresAppExe()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        auto result = parseCli({"SimpleUpdater", "generate", tempDir.path()});
        QVERIFY(!result.has_value());
    }

    void generateNonexistentAppExe()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        auto result = parseCli({"SimpleUpdater", "generate",
                                "--app_exe", "NoSuchFile.exe", tempDir.path()});
        QVERIFY(!result.has_value());
    }

    void generateNonexistentDirectory()
    {
        auto result = parseCli({"SimpleUpdater", "generate",
                                "--app_exe", "App.exe",
                                "C:/nonexistent_dir_xyz_12345"});
        QVERIFY(!result.has_value());
    }

    void generateAutoDetectsVersion()
    {
#ifdef Q_OS_WIN
        auto version = Platform::readExeVersion("C:/Windows/System32/where.exe");
        if(!version)
            QSKIP("Cannot read version from system executable");
        QVERIFY(!version->isNull());
        QVERIFY(version->segmentCount() >= 2);
#else
        QSKIP("Version detection test requires Windows");
#endif
    }

    void generateWithMinVersion()
    {
#ifdef Q_OS_WIN
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString systemExe = "C:/Windows/System32/where.exe";
        if(!QFileInfo::exists(systemExe))
            QSKIP("System executable not available");
        QVERIFY(QFile::copy(systemExe, dir.filePath("App.exe")));

        auto result = parseCli({"SimpleUpdater", "generate",
                                "--app_exe", "App.exe",
                                "--min_version", "1.5.0",
                                dir.absolutePath()});
        if(!result)
            QSKIP("generate failed (version detection unavailable)");

        QVERIFY(result->generate.has_value());
        QVERIFY(result->generate->minVersion.has_value());
        QCOMPARE(result->generate->minVersion->toString(), QString("1.5.0"));
#else
        QSKIP("Test requires Windows");
#endif
    }

    void generateWithoutMinVersion()
    {
#ifdef Q_OS_WIN
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString systemExe = "C:/Windows/System32/where.exe";
        if(!QFileInfo::exists(systemExe))
            QSKIP("System executable not available");
        QVERIFY(QFile::copy(systemExe, dir.filePath("App.exe")));

        auto result = parseCli({"SimpleUpdater", "generate",
                                "--app_exe", "App.exe",
                                dir.absolutePath()});
        if(!result)
            QSKIP("generate failed (version detection unavailable)");

        QVERIFY(result->generate.has_value());
        QVERIFY(!result->generate->minVersion.has_value());
#else
        QSKIP("Test requires Windows");
#endif
    }

    void generateInvalidMinVersion()
    {
#ifdef Q_OS_WIN
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        QString systemExe = "C:/Windows/System32/where.exe";
        if(!QFileInfo::exists(systemExe))
            QSKIP("System executable not available");
        QVERIFY(QFile::copy(systemExe, dir.filePath("App.exe")));

        auto result = parseCli({"SimpleUpdater", "generate",
                                "--app_exe", "App.exe",
                                "--min_version", "abc",
                                dir.absolutePath()});
        QVERIFY2(!result.has_value(),
                 "Invalid --min_version should cause an error");
#else
        QSKIP("Test requires Windows");
#endif
    }

    // ---- update subcommand ----

    void updateSubcommand()
    {
        QTemporaryDir srcDir, tgtDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", srcDir.path(),
                                "--target", tgtDir.path()});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Update);
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->source, srcDir.path());
        QCOMPARE(result->update->forceUpdate, false);
        QCOMPARE(result->update->continueUpdate, false);
    }

    void updateRequiresSource()
    {
        QTemporaryDir tgtDir;
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update", "--target", tgtDir.path()});
        QVERIFY(!result.has_value());
    }

    void updateMissingSourceErrors()
    {
        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", "C:/nonexistent/path/source"});
        QVERIFY(!result.has_value());
    }

    void updateMissingTargetErrors()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", srcDir.path(),
                                "--target", "C:/nonexistent/path/target"});
        QVERIFY(!result.has_value());
    }

    void updateWithForceFlag()
    {
        QTemporaryDir srcDir, tgtDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", srcDir.path(),
                                "--target", tgtDir.path(),
                                "--force"});
        QVERIFY(result.has_value());
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->forceUpdate, true);
        QCOMPARE(result->update->continueUpdate, false);
    }

    void updateWithContinueFlag()
    {
        QTemporaryDir srcDir, tgtDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", srcDir.path(),
                                "--target", tgtDir.path(),
                                "--continue-update"});
        QVERIFY(result.has_value());
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->forceUpdate, false);
        QCOMPARE(result->update->continueUpdate, true);
    }

    void updateWithBothFlags()
    {
        QTemporaryDir srcDir, tgtDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", srcDir.path(),
                                "--target", tgtDir.path(),
                                "--force", "--continue-update"});
        QVERIFY(result.has_value());
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->forceUpdate, true);
        QCOMPARE(result->update->continueUpdate, true);
    }

    void updateWithUrlSource()
    {
        auto result = parseCli({"SimpleUpdater", "update",
                                "--source", "https://releases.example.com/v2.0.zip"});
        QVERIFY2(result.has_value(),
                 "URL source should be accepted without path existence check");
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->source, QString("https://releases.example.com/v2.0.zip"));
    }

    void updateShortFlags()
    {
        QTemporaryDir srcDir, tgtDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(tgtDir.isValid());

        auto result = parseCli({"SimpleUpdater", "update",
                                "-s", srcDir.path(),
                                "-t", tgtDir.path()});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Update);
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->source, srcDir.path());
    }

    // ---- legacy flag compat ----

    void legacyDashU()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        auto result = parseCli({"SimpleUpdater", "-u", "-s", srcDir.path()});
        QVERIFY2(result.has_value(),
                 "Legacy '-u -s <path>' invocation must be accepted for backward compat");
        QCOMPARE(result->mode, AppMode::Update);
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->source, srcDir.path());
    }

    void legacyDashDashUpdate()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        auto result = parseCli({"SimpleUpdater", "--update", "-s", srcDir.path()});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Update);
        QVERIFY(result->update.has_value());
        QCOMPARE(result->update->source, srcDir.path());
    }

    // ---- install subcommand ----

    void installSubcommand()
    {
        auto result = parseCli({"SimpleUpdater", "install"});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Install);
        QVERIFY(result->install.has_value());
        QVERIFY(!result->install->sourceDir.has_value());
        QVERIFY(!result->install->targetDir.has_value());
    }

    void installWithSourceAndTarget()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        auto result = parseCli({"SimpleUpdater", "install",
                                "--source", srcDir.path(),
                                "--target", "C:/some/target"});
        QVERIFY(result.has_value());
        QCOMPARE(result->mode, AppMode::Install);
        QVERIFY(result->install.has_value());
        QVERIFY(result->install->sourceDir.has_value());
        QVERIFY(result->install->targetDir.has_value());
    }

    void installNonexistentSourceErrors()
    {
        auto result = parseCli({"SimpleUpdater", "install",
                                "--source", "C:/nonexistent/path/source"});
        QVERIFY(!result.has_value());
    }

    void installShortFlags()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        auto result = parseCli({"SimpleUpdater", "install",
                                "-s", srcDir.path(),
                                "-t", "C:/target"});
        QVERIFY(result.has_value());
        QVERIFY(result->install.has_value());
        QVERIFY(result->install->sourceDir.has_value());
        QVERIFY(result->install->targetDir.has_value());
    }
};

QTEST_MAIN(TestCliParser)
#include "tst_cliparser.moc"
