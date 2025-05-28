#include "mainwindow.h"
#include "filehandler.h"
#include <QSettings>
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
#include <QUuid>
#include "desktopshortcut.h"


MainWindow::MainWindow(std::optional<QDir> sourceLocation, std::optional<QDir> targetLocation,
                       bool isInstall, QWidget *parent)
    : QMainWindow(parent), fileHandler(new FileHandler(this))
{
    if(isInstall)
        sourceLocation = sourceLocation ? sourceLocation : QDir();
    else
        targetLocation = targetLocation ? targetLocation : QDir();


    if(sourceLocation)
    {
        if(!sourceLocation->exists("updateInfo.ini"))
            FileHandler::generateInfoFile(sourceLocation.value(), {}, {}, true, false);
        sourceInfo.emplace(sourceLocation->filePath("updateInfo.ini"), QSettings::IniFormat);
    }
    if(targetLocation)
    {
        if(!targetLocation->exists("updateInfo.ini"))
            FileHandler::generateInfoFile(targetLocation.value(), {}, {}, false, false);
        targetInfo.emplace(targetLocation->filePath("updateInfo.ini"), QSettings::IniFormat);
    }

    bool forceUpdate = true;
    if(sourceInfo && sourceInfo->contains("SETTINGS/force_update"))
        forceUpdate = sourceInfo->value("SETTINGS/force_update").toBool();

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

    QString newVersion = sourceInfo ? sourceInfo->value("SETTINGS/app_version").toString() : "?.?.?";
    QString appName = sourceInfo ? sourceInfo->value("SETTINGS/app_exe").toString() : "Your application";
    installLayout->addWidget(new QLabel("<b>"+tr("Installation Required")+"</b>"));
    installLayout->addWidget(new QLabel(tr("%1 (%2) located in %3.\n"
                                           "Please select the directory where the application will be installed.")
                                            .arg(appName, newVersion, sourceLocation ? sourceLocation->absolutePath() : "")));

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

    installLayout->addStretch();  // pushes content up
    stackedWidget->addWidget(installScreen);

    // --------- Update Screen ---------
    updateScreen = new QWidget();
    QVBoxLayout* updateLayout = new QVBoxLayout(updateScreen);
    updateLayout->setSpacing(12);
    updateLayout->addWidget(new QLabel("<b>"+tr("Update Detected")+"</b>"));
    QString oldVersion = targetInfo ? targetInfo->value("SETTINGS/app_version").toString() : "?.?.?";

    qDebug()<<"Application name:"<<appName<<newVersion;
    if(appName.isEmpty())
        appName = "Your application";
    QString forceUpdateText = "This update can be skipped, press \"Update Later\" to launch application without updating.";
    if(forceUpdate)
        forceUpdateText = "This update is mandatory and cannot be skipped!"+QString::number((int)sourceInfo->value("SETTINGS/force_update").toBool());
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
    connect(updateLaterButton, &QPushButton::clicked, [this, targetLocation](){
        QDir dir;
        if(targetLocation)
            dir = targetLocation.value();
        finalize(dir, {"--update_skipped"});
        QApplication::exit(0);
    });
    connect(browseButton, &QPushButton::clicked, this, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory");
        if (!dir.isEmpty())
            pathEdit->setText(dir);
    });

    connect(continueButton, &QPushButton::clicked, this, [this, isInstall, sourceLocation, targetLocation](){
        if(isInstall && sourceLocation)
        {
            auto installationDir = QDir(pathEdit->text());
            if(!installationDir.mkpath(pathEdit->text()))
            {
                QMessageBox::critical(this, "Invalid directory",
                                      "The provided directory cannot be created or is inaccesible, check permissions.");
                return;
            }
            installApplication(sourceLocation.value(), installationDir);
        }
        else
        {
            Q_ASSERT(sourceLocation);
            QDir dir;
            if(targetLocation)
                dir = targetLocation.value();
            updateApplication(sourceLocation.value(), dir);
        }

        stackedWidget->setCurrentIndex(2);
        quitButton->setVisible(false);
        continueButton->setVisible(false);
        updateLaterButton->setVisible(false);
    });

    connect(cancelButton, &QPushButton::clicked, [this](){
        auto answer = QMessageBox::question(this, tr("Interrupt Operation?"),
                                            tr("Are you sure you want to cancel the current operation?\n\n"
                                               "Interrupting it at this stage may leave the application in an unusable or inconsistent state."),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
        if (answer == QMessageBox::Yes)
            fileHandler->cancel();
    });

    connect(fileHandler, &FileHandler::progressUpdated, this, [this](QPair<QString, bool> result){
        progressBar->setValue(progressBar->value()+1);
        auto msg = result.first + (result.second ? "    OK" : "    ERROR");
        auto color = result.second ? Qt::white : Qt::red;
        logMessage(msg, color);
    });

    connect(this, &MainWindow::processFinished, this, [this, targetLocation, isInstall](bool success){
        QString operation = isInstall ? "INSTALLATION" : "UPDATE";
        if(!success)
        {
            logMessage(operation + " FAILED", Qt::red);
            QMessageBox::critical(this, tr("Copy failed!"),
                                  tr("Installation/update process failed, "
                                     "please refer to log to see what files were not possible to copy succesfully.\n"
                                     "Backup"));

            cancelButton->setVisible(false);
            quitButton->setVisible(true);
        }
        else
        {
            logMessage(operation + " COMPLETE", Qt::green);
            progressBar->setValue(progressBar->maximum());
            QDir dir;
            if(isInstall)
                dir = pathEdit->text();
            else if(targetLocation)
                dir = targetLocation.value();

            QThread::msleep(300);
            if(progressBar->maximum()==0)
                progressBar->hide();

            if(auto success = finalize(dir, {isInstall ? "--installation" : "--update"}, isInstall))
                QApplication::quit();
            else
                quitButton->setVisible(true);
        }
    });
    connect(fileHandler, &FileHandler::cancelled, this, [this](){QApplication::quit();});
}

void MainWindow::installApplication(QDir sourceDir, QDir targetDir)
{
    int sourceFileCount = 0;
    if(sourceInfo && sourceInfo->contains("SETTINGS/file_count"))
    {
        sourceFileCount = sourceInfo->value("SETTINGS/file_count").toInt();
        qDebug()<<"Source file_count = "<<sourceFileCount<<"from"<<sourceInfo->fileName();
    }
    if(sourceFileCount == 0)
    {
        sourceFileCount = FileHandler::getFileCountRecursive(sourceDir);
        qDebug()<<"Source file_count = "<<sourceFileCount<<"(from getFileCountRecursive)";
    }

    int targetFileCount = 0;
    if(targetInfo && targetInfo->contains("SETTINGS/file_count"))
    {
        targetFileCount = targetInfo->value("SETTINGS/file_count").toInt();
        qDebug()<<"Target file_count = "<<targetFileCount<<"from"<<targetInfo->fileName();
    }
    if(targetFileCount == 0)
    {
        targetFileCount = FileHandler::getFileCountRecursive(targetDir);
        qDebug()<<"Target file_count = "<<targetFileCount<<"(from getFileCountRecursive)";
    }

    progressBar->setRange(0, sourceFileCount + targetFileCount-1); //first we will do backup, then copy so max is both operations

    targetInfo.reset();
    QThreadPool::globalInstance()->start([this, targetDir, sourceDir](){

        //create backup folder
        QDir parentDir = targetDir;
        parentDir.cdUp();
        QString backupName = targetDir.dirName() + "_backup";
        QDir backupDir = parentDir.filePath(backupName);
        qDebug()<<"Creating a backup in:"<<backupDir.absolutePath();
        if(backupDir.exists())
            backupDir.removeRecursively();
        parentDir.mkdir(backupName);

        logMessage("CREATING BACKUP...", Qt::green);
        bool copySuccess = false;
        bool backupSuccess = fileHandler->copyDirectoryRecursively(targetDir, backupDir);
        logMessage("BACKUP"+QString((backupSuccess ? " SUCCESS" : " FAILED, ABORTING...")), backupSuccess ? Qt::green : Qt::red);
        if(backupSuccess)
        {
            logMessage("INSTALLING FILES...", Qt::green);
            if(fileHandler->copyDirectoryRecursively(sourceDir, targetDir))
                copySuccess = true;
            else
                fileHandler->copyDirectoryRecursively(backupDir, targetDir);
        }
        if(backupDir.exists() && copySuccess)
            backupDir.removeRecursively();
        emit processFinished(copySuccess);
    });
}

void MainWindow::updateApplication(QDir sourceDir, QDir targetDir)
{
    bool missingInfo = !sourceInfo || !targetInfo;
    bool fullUpdate = sourceInfo && sourceInfo->value("SETTINGS/full_update").toBool();

    if(missingInfo || fullUpdate)
        installApplication(sourceDir, targetDir);
    else
    {
        //FIND FILE DIFFIRENCES
        sourceInfo->beginGroup("FILE_HASHES");
        targetInfo->beginGroup("FILE_HASHES");
        QStringList sourceFiles = sourceInfo->allKeys();
        QStringList targetFiles = targetInfo->allKeys();


        //Get new files that are diffirent or new
        QStringList diffirentFiles;
        for(auto file : sourceFiles)
            if(sourceInfo->value(file) != targetInfo->value(file))
                diffirentFiles.append(file.replace("|", "/"));

        //make sure updateInfo.ini is copied, becouse it might not hash itself
        diffirentFiles.append(sourceDir.relativeFilePath("updateInfo.ini"));

        //Get files to backup
        QStringList backupFiles;
        for(const auto& relPath : diffirentFiles)
            if(QFile::exists(targetDir.filePath(relPath)))
                backupFiles.append(relPath);

        //Get files that no longer exist in new version, we are not removing folders,
        // TODO: check for folders that are not in sourceDir, or maybe hash folders too!
        QStringList filesToRemove;
        for(auto file : targetFiles)
            if(!sourceInfo->contains(file))
                if(!file.endsWith("updateInfo.ini")) //dont remove updateInfo even if it wasnt hashed
                    filesToRemove.append(file.replace("|", "/"));

        progressBar->setRange(0, diffirentFiles.count()+backupFiles.count()+filesToRemove.count()-1);

        //COPY AND REMOVE FILES
        qDebug()<<"Copying files:\n"<<diffirentFiles<<"\n";
        qDebug()<<"Removing files:\n"<<filesToRemove<<"\n";

        sourceInfo->endGroup();
        targetInfo->endGroup();
        targetInfo.reset();
        QThreadPool::globalInstance()->start([this, targetDir, sourceDir, diffirentFiles,
                                              filesToRemove, backupFiles](){
            //create backup folder
            QString uniqueFolderName = "backup" + QUuid::createUuid().toString(QUuid::Id128);
            QDir backupDir = targetDir.filePath(uniqueFolderName);
            qDebug()<<"Creating a backup in:"<<uniqueFolderName;
            if(backupDir.exists())
                backupDir.removeRecursively();
            targetDir.mkdir(uniqueFolderName);

            //perform backup and abort if failed
            bool copySuccess = false;
            logMessage("CREATING BACKUP...", Qt::green);
            bool backupSuccess = fileHandler->copyFiles(targetDir, backupDir, backupFiles, true);
            logMessage("BACKUP"+QString((backupSuccess ? " SUCCESS" : " FAILED, ABORTING...")), backupSuccess ? Qt::green : Qt::red);
            if(backupSuccess)
            {
                if(fileHandler->copyFiles(sourceDir, targetDir, diffirentFiles, true))
                {
                    if(!fileHandler->removeFiles(targetDir, filesToRemove))
                        qWarning()<<"Failed to remove some files!";
                    copySuccess = true;
                }
                else
                    fileHandler->copyFiles(backupDir, targetDir, backupFiles, false);
            }
            if(backupDir.exists())
                backupDir.removeRecursively();
            emit processFinished(copySuccess);
        });
    }
}

void MainWindow::logMessage(QString msg, QColor color)
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

    if (atBottom)
        vScroll->setValue(vScroll->maximum());
}

bool MainWindow::finalize(QDir appDirectory, QStringList args, bool makeShortcut)
{
    bool success = false;
    targetInfo.emplace(appDirectory.absoluteFilePath("updateInfo.ini"), QSettings::IniFormat);
    if(targetInfo && targetInfo->contains("SETTINGS/app_exe"))
    {
        QString relativeAppPath = targetInfo->value("SETTINGS/app_exe").toString();

        QString absolutePath = appDirectory.absoluteFilePath(relativeAppPath);
        auto fileInfo = QFileInfo(absolutePath);
        if(fileInfo.exists())
        {
            // TODO: split this function, move this elsewhere
            if(makeShortcut)
            {
                qDebug()<<"Creating shortcut for"<<absolutePath;
                if(!createShortcut(absolutePath, fileInfo.completeBaseName()))
                    qDebug()<<"Failed to create shortcut!";
            }
            logMessage("Launching application: "+absolutePath, Qt::yellow);
            qDebug()<<"Launching application"<<absolutePath;
            success = QProcess::startDetached(absolutePath, args);
            if(!success)
            {
                logMessage("Failed to start "+absolutePath, Qt::yellow);
                qWarning() << "Failed to start"<<absolutePath;
            }
        }
        else
        {
            logMessage("Cannot find app_exe= "+relativeAppPath+" in destination/target directory= "+appDirectory.absolutePath()+" after copy operation", Qt::yellow);
            qWarning()<<"Cannot find app_exe="<<relativeAppPath<<"in destination/target directory="<<appDirectory.absolutePath()<<"after copy operation";
        }
    }
    return success;
}
