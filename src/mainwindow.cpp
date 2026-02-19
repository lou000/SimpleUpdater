#include "mainwindow.h"
#include "updatecontroller.h"
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>
#include <QProcess>
#include <QFileDialog>
#include <QThreadPool>
#include <QScrollBar>
#include <QTimer>


MainWindow::MainWindow(const CliResult& config, QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new UpdateController(this))
{
    bool isInstall = (config.mode == AppMode::Install);

    if(isInstall)
    {
        auto& inst = config.install.value();
        m_controller->setSourceDir(inst.sourceDir.value_or(QDir()));
        if(inst.targetDir)
            m_controller->setTargetDir(inst.targetDir.value());
        m_controller->setInstallMode(true);
    }
    else
    {
        auto& upd = config.update.value();
        if(isUrl(upd.source))
            m_controller->setSourceUrl(upd.source);
        else
            m_controller->setSourceDir(QDir(upd.source));
        m_controller->setTargetDir(upd.targetDir);
        m_controller->setForceUpdate(upd.forceUpdate);
        m_controller->setContinueUpdate(upd.continueUpdate);
    }

    m_controller->prepare();

    bool forceUpdate = m_controller->isMandatory();
    const auto& srcManifest = m_controller->sourceManifest();
    auto tgtVersion = m_controller->targetVersion();

    resize(600, 400);
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    setFixedSize(size());
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    stackedWidget = new QStackedWidget();

    // --------- Install Screen ---------
    installScreen = new QWidget();
    QVBoxLayout* installLayout = new QVBoxLayout(installScreen);
    installLayout->setSpacing(12);

    QString newVersion = !srcManifest.version.isNull()
                             ? srcManifest.version.toString() : "?.?.?";
    QString appName = !srcManifest.appExe.isEmpty()
                          ? srcManifest.appExe : "Your application";
    installLayout->addWidget(new QLabel("<b>"+tr("Installation Required")+"</b>"));

    QString sourceDesc;
    if(isInstall && config.install->sourceDir)
        sourceDesc = config.install->sourceDir->absolutePath();
    installLayout->addWidget(new QLabel(tr("%1 (%2) located in %3.\n"
                                           "Please select the directory where the application will be installed.")
                                            .arg(appName, newVersion, sourceDesc)));

    QHBoxLayout* pathLayout = new QHBoxLayout();
    pathEdit = new QLineEdit();
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(path);
    dir.cdUp();
    QString customPath = dir.filePath(QDir(QApplication::applicationDirPath()).dirName());
    pathEdit->setText(customPath);
    browseButton = new QPushButton(tr("Browse..."));
    pathLayout->addWidget(pathEdit);
    pathLayout->addWidget(browseButton);
    installLayout->addLayout(pathLayout);

    installLayout->addStretch();
    stackedWidget->addWidget(installScreen);

    // --------- Update Screen ---------
    updateScreen = new QWidget();
    QVBoxLayout* updateLayout = new QVBoxLayout(updateScreen);
    updateLayout->setSpacing(12);
    updateLayout->addWidget(new QLabel("<b>"+tr("Update Detected")+"</b>"));
    QString oldVersion = !tgtVersion.isNull() ? tgtVersion.toString() : "?.?.?";

    qDebug()<<"Application name:"<<appName<<newVersion;
    if(appName.isEmpty())
        appName = "Your application";
    QString forceUpdateText = "This update can be skipped, press \"Update Later\" to launch application without updating.";
    if(forceUpdate)
        forceUpdateText = "This update is mandatory and cannot be skipped!";
    updateLayout->addWidget(new QLabel(tr("%1 (%2) will now be updated to the latest version (%3).\n%4")
                                           .arg(appName, oldVersion, newVersion, forceUpdateText)));
    updateLayout->addStretch();
    stackedWidget->addWidget(updateScreen);

    // --------- Progress Screen ---------
    progressScreen = new QWidget();
    QVBoxLayout* progressLayout = new QVBoxLayout(progressScreen);
    progressLayout->setSpacing(12);
    progressLayout->addWidget(new QLabel("<b>"+tr("Progress")+"</b>"));
    progressLayout->addWidget(new QLabel(tr("Installation/Update is in progress. Please wait...")));

    progressBar = new QProgressBar();
    QStyle *style = QStyleFactory::create("Fusion");
    if(style)
        progressBar->setStyle(style);
    progressBar->setStyleSheet("background-color: rgb(70, 70 ,70)");
    progressBar->setTextVisible(true);
    progressBar->setRange(0, 100);
    progressLayout->addWidget(progressBar);

    logBox = new QTextEdit();
    logBox->setReadOnly(true);
    logBox->setMinimumHeight(150);
    QFont font("Courier");
    font.setStyleHint(QFont::Monospace);
    logBox->setFont(font);
    progressLayout->addWidget(logBox);

    stackedWidget->addWidget(progressScreen);
    mainLayout->addWidget(stackedWidget, 1);

    // --------- Shared Buttons ---------
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    updateLaterButton = new QPushButton(tr("Update Later"));
    cancelButton = new QPushButton(tr("Cancel"));
    continueButton = new QPushButton(tr("Continue"));
    quitButton = new QPushButton(tr("Quit"));
    continueButton->setDefault(true);
    buttonLayout->addWidget(updateLaterButton);
    buttonLayout->addWidget(quitButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(continueButton);
    updateLaterButton->setVisible(!forceUpdate);
    cancelButton->setVisible(false);

    mainLayout->addLayout(buttonLayout);

    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    if(isInstall)
    {
        stackedWidget->setCurrentIndex(0);
        updateLaterButton->setVisible(false);
    }
    else
        stackedWidget->setCurrentIndex(1);

    connect(quitButton, &QPushButton::clicked, [](){QApplication::quit();});

    connect(updateLaterButton, &QPushButton::clicked, [this](){
        auto& srcManifest = m_controller->sourceManifest();
        if(!srcManifest.appExe.isEmpty())
        {
            QDir tgtDir = m_controller->targetDir();
            QString absPath = tgtDir.absoluteFilePath(srcManifest.appExe);
            if(QFileInfo::exists(absPath))
                QProcess::startDetached(absPath, {"--update_skipped"}, tgtDir.absolutePath());
        }
        QApplication::quit();
    });

    connect(browseButton, &QPushButton::clicked, this, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory");
        if(!dir.isEmpty())
            pathEdit->setText(dir);
    });

    connect(continueButton, &QPushButton::clicked, this, [this, isInstall](){
        if(isInstall)
        {
            auto installationDir = QDir(pathEdit->text());
            if(!installationDir.mkpath(pathEdit->text()))
            {
                QMessageBox::critical(this, "Invalid directory",
                                      "The provided directory cannot be created or is inaccessible, check permissions.");
                return;
            }
            m_controller->setTargetDir(installationDir);
            m_controller->prepare();
        }

        stackedWidget->setCurrentIndex(2);
        quitButton->setVisible(false);
        continueButton->setVisible(false);
        updateLaterButton->setVisible(false);
        cancelButton->setVisible(true);

        QThreadPool::globalInstance()->start([this](){
            m_controller->execute();
        });
    });

    connect(cancelButton, &QPushButton::clicked, [this](){
        auto answer = QMessageBox::question(this, tr("Interrupt Operation?"),
                                            tr("Are you sure you want to cancel the current operation?\n\n"
                                               "Interrupting it at this stage may leave the application in an unusable or inconsistent state."),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
        if(answer == QMessageBox::Yes)
            m_controller->cancel();
    });

    connect(m_controller, &UpdateController::progressUpdated, this, [this](const QString& description, bool success){
        if(progressBar->value() < progressBar->maximum())
            progressBar->setValue(progressBar->value()+1);
        auto msg = description + (success ? "    OK" : "    ERROR");
        auto color = success ? Qt::white : Qt::red;
        logMessage(msg, color);
    });

    connect(m_controller, &UpdateController::progressRangeChanged, this, [this](int min, int max){
        progressBar->setRange(min, max);
    });

    connect(m_controller, &UpdateController::downloadProgress, this, [this](qint64 received, qint64 total){
        if(total > 0)
        {
            progressBar->setRange(0, 100);
            progressBar->setValue(static_cast<int>(received * 100 / total));
        }
        else
        {
            progressBar->setRange(0, 0);
        }
    });

    connect(m_controller, &UpdateController::statusMessage, this, &MainWindow::logMessage);

    connect(m_controller, &UpdateController::processLockDetected, this, [this](const QStringList& processes){
        QString msg = tr("The following processes are locking files that need to be updated:\n\n");
        for(const auto& p : processes)
            msg += "  " + p + "\n";
        msg += tr("\nClose these processes and click Retry, or click Kill All to terminate them.");

        QMessageBox box(this);
        box.setWindowTitle(tr("Files Locked"));
        box.setText(msg);
        box.setIcon(QMessageBox::Warning);
        auto* retryBtn = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
        auto* killBtn = box.addButton(tr("Kill All"), QMessageBox::DestructiveRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(retryBtn);
        box.exec();

        if(box.clickedButton() == killBtn)
            m_controller->respondToLockPrompt(LockAction::KillAll);
        else if(box.clickedButton() == retryBtn)
            m_controller->respondToLockPrompt(LockAction::Retry);
        else
            m_controller->respondToLockPrompt(LockAction::Cancel);
    });

    connect(m_controller, &UpdateController::error, this, [this](const QString& message){
        logMessage(message, Qt::red);
    });

    connect(m_controller, &UpdateController::selfUpdateRelaunch, this, [](){
        QApplication::quit();
    });

    connect(m_controller, &UpdateController::updateFinished, this, [this, isInstall](bool success){
        cancelButton->setVisible(false);
        QString operation = isInstall ? "INSTALLATION" : "UPDATE";

        if(!success)
        {
            logMessage(operation + " FAILED", Qt::red);
            if(!m_controller->isCancelled())
            {
                QMessageBox::critical(this, tr("Operation failed"),
                                      tr("Installation/update process failed, "
                                         "please refer to the log to see which files could not be copied successfully.\n"
                                         "A backup was created before the operation and can be used for recovery."));
            }
            quitButton->setVisible(true);
        }
        else
        {
            logMessage(operation + " COMPLETE", Qt::green);
            progressBar->setValue(progressBar->maximum());
            QTimer::singleShot(300, this, [this](){
                QApplication::quit();
            });
        }
    });

    if(!isInstall && config.update->continueUpdate)
    {
        stackedWidget->setCurrentIndex(2);
        quitButton->setVisible(false);
        continueButton->setVisible(false);
        updateLaterButton->setVisible(false);
        cancelButton->setVisible(true);

        QThreadPool::globalInstance()->start([this](){
            m_controller->execute();
        });
    }
}

MainWindow::~MainWindow()
{
    m_controller->cancel();
    QThreadPool::globalInstance()->waitForDone();
}

void MainWindow::logMessage(const QString& msg, const QColor& color)
{
    QScrollBar* vScroll = logBox->verticalScrollBar();
    bool atBottom = (vScroll->value() == vScroll->maximum());

    QTextCharFormat format;
    format.setForeground(color);

    QTextCursor cursor = logBox->textCursor();
    cursor.movePosition(QTextCursor::End);

    QFontMetrics metrics(logBox->font());
    int maxWidth = logBox->viewport()->width();
    QString elidedText = metrics.elidedText(msg, Qt::ElideLeft, maxWidth);

    cursor.insertText("\n"+elidedText, format);

    if(atBottom)
        vScroll->setValue(vScroll->maximum());
}
