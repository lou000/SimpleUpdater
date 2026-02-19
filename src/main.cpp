#include "cliparser.h"
#include "mainwindow.h"
#include "manifest.h"
#include "version.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QStyleHints>

static MainWindow* g_mainWindow = nullptr;

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx)
    if(type == QtDebugMsg || type == QtInfoMsg)
        return;

    if(g_mainWindow)
    {
        QColor color = (type >= QtCriticalMsg) ? Qt::red : Qt::yellow;
        QMetaObject::invokeMethod(g_mainWindow, "logMessage",
                                  Qt::AutoConnection,
                                  Q_ARG(QString, msg),
                                  Q_ARG(QColor, color));
    }
    else if(type >= QtCriticalMsg)
    {
        QMessageBox::critical(nullptr, QStringLiteral("SimpleUpdater"), msg);
    }

    if(type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qInstallMessageHandler(messageHandler);
    QApplication::setApplicationVersion(APP_VERSION);
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    QString selfPath = QCoreApplication::applicationFilePath();
    QString oldPath = selfPath + "_old";
    if(QFile::exists(oldPath))
        QFile::remove(oldPath);

    auto config = parseCli(a);
    if(!config)
        return 1;

    if(config->mode == AppMode::Generate)
    {
        auto& gen = config->generate.value();
        auto manifest = generateManifest(gen.directory, gen.appExe, gen.minVersion);
        return manifest ? 0 : 1;
    }

    MainWindow w(*config);
    g_mainWindow = &w;
    w.show();
    int ret = a.exec();
    g_mainWindow = nullptr;
    return ret;
}
