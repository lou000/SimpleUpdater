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

MainWindow::MainWindow(const std::optional<QDir>& sourceLocation, const std::optional<QDir>& targetLocation, QWidget *parent)
    : QMainWindow(parent), fileHandler(new FileHandler(this))
{

    if(sourceLocation)
        if(sourceLocation->exists("updateInfo.ini"))
            sourceInfo.emplace(sourceLocation->filePath("updateInfo.ini"), QSettings::IniFormat);

    if(targetLocation)
    {
        if(!targetLocation->exists("updateInfo.ini"))
            FileHandler::generateInfoFile(targetLocation.value(), {}, {}, false, false);
        sourceInfo.emplace(targetLocation->filePath("updateInfo.ini"), QSettings::IniFormat);
    }
    else
    {
        if(!QDir().exists("updateInfo.ini"))
            FileHandler::generateInfoFile(targetLocation.value(), {}, {}, false, false);
        sourceInfo.emplace(QDir().filePath("updateInfo.ini"), QSettings::IniFormat);
    }

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

    installLayout->addWidget(new QLabel("<b>"+tr("Installation Required")+"</b>"));
    installLayout->addWidget(new QLabel(tr("Please select the directory where the application will be installed.")));

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
    updateLayout->addWidget(new QLabel(tr("Your application will now be updated to the latest version.")));
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
    progressLayout->addWidget(logBox);

    stackedWidget->addWidget(progressScreen);
    mainLayout->addWidget(stackedWidget, 1);

    // --------- Shared Buttons ---------
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    updateLater = new QPushButton(tr("Update Later"));
    cancelButton = new QPushButton(tr("Cancel"));
    continueButton = new QPushButton(tr("Continue"));
    quitButton = new QPushButton(tr("Quit"));
    continueButton->setDefault(true);
    buttonLayout->addWidget(updateLater);
    buttonLayout->addWidget(quitButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(continueButton);
    updateLater->setVisible(false);
    cancelButton->setVisible(false);

    mainLayout->addLayout(buttonLayout);

    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    bool installation = false;
    if(!sourceLocation && !targetLocation)
    {
        installation = true;
        stackedWidget->setCurrentIndex(0);
    }
    else
        stackedWidget->setCurrentIndex(1);

    connect(quitButton, &QPushButton::clicked, [](){QApplication::quit();});
    connect(browseButton, &QPushButton::clicked, this, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory");
        if (!dir.isEmpty())
            pathEdit->setText(dir);
    });

    connect(continueButton, &QPushButton::clicked, this, [this, installation, sourceLocation, targetLocation](){
        if(installation)
        {
            auto installationDir = QDir(pathEdit->text());
            if(!installationDir.mkpath(pathEdit->text()))
            {
                QMessageBox::critical(this, "Invalid directory",
                                      "The provided directory cannot be created or is inaccesible, check permissions.");
                return;
            }
            installApplication(QDir(), installationDir);
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
        continueButton->setVisible(false);
        quitButton->setVisible(false);
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
    connect(fileHandler, &FileHandler::progressUpdated, this, [this](QPair<QString, bool> results){
        progressBar->setValue(progressBar->value()+1);

        QScrollBar* vScroll = logBox->verticalScrollBar();
        bool atBottom = (vScroll->value() == vScroll->maximum());

        QColor textColor = results.second ? Qt::white : Qt::red;
        QTextCharFormat format;
        format.setForeground(textColor);

        QTextCursor cursor = logBox->textCursor();
        cursor.movePosition(QTextCursor::End);

        QFontMetrics metrics(logBox->font());
        int maxWidth = logBox->viewport()->width();
        QString elidedText = metrics.elidedText(results.first, Qt::ElideLeft, maxWidth - 80);

        QString resultStr = results.second ? "OK" : "ERROR";
        cursor.insertText(elidedText + "    " + resultStr + "\n", format);

        if (atBottom)
            vScroll->setValue(vScroll->maximum());

    });
    connect(fileHandler, &FileHandler::copyFinished, this, [this, targetLocation, installation](bool success){
        if(!success)
        {
            QMessageBox::critical(this, tr("Copy failed!"),
                                  tr("Installation/update process failed, "
                                  "please refer to log to see what files were not possible to copy succesfully."));

            cancelButton->setVisible(false);
            quitButton->setVisible(true);
        }
        else
        {
            if(sourceInfo && sourceInfo->contains("SETTINGS/app_exe"))
            {
                QString relativeAppPath = sourceInfo->value("SETTINGS/app_exe").toString();
                QDir dir;
                if(installation)
                    dir = pathEdit->text();
                else if(targetLocation)
                    dir = targetLocation.value();


                QString absolutePath = dir.absoluteFilePath(relativeAppPath);

                if(QFile::exists(absolutePath))
                {
                    //TODO: maybe add arguments if it was installation or update etc.
                    qDebug()<<"Launching application"<<absolutePath;
                    bool success = QProcess::startDetached(absolutePath, {});
                    if(!success)
                        qWarning() << "Failed to start"<<absolutePath;
                }
                else
                    qWarning()<<"Cannot find app_exe="<<relativeAppPath<<" in destination/target directory="<<dir.absolutePath()<<"after copy operation";
            }
            QThread::msleep(500);
            QApplication::quit();
        }
    });
    connect(fileHandler, &FileHandler::cancelled, this, [this](){
        QApplication::quit();
    });
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

    progressBar->setRange(0, sourceFileCount + targetFileCount-1+2); //first we will do backup, then copy so max is both operations

    QThreadPool::globalInstance()->start([this, targetDir, sourceDir](){
        fileHandler->copyDirectoryRecursively(sourceDir, targetDir);
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
        QStringList diffirentFiles;
        sourceInfo->beginGroup("FILE_HASHES");
        targetInfo->beginGroup("FILE_HASHES");
        QStringList sourceFiles = sourceInfo->allKeys();
        QStringList targetFiles = targetInfo->allKeys();

        //Get new files that are diffirent or new
        int newFiles = 0;
        for(auto file : sourceFiles)
        {
            if(!targetInfo->contains(file))
                newFiles++;
            if(sourceInfo->value(file) != targetInfo->value(file))
                diffirentFiles.append(file.replace("|", "/"));
        }
        //Get files that no longer exist in new version
        QStringList filesToRemove;
        for(auto file : targetFiles)
            if(!sourceInfo->contains(file))
                filesToRemove.append(file.replace("|", "/"));

        int filesToBackup = diffirentFiles.count()-newFiles;
        progressBar->setRange(0, diffirentFiles.count()+filesToBackup+filesToRemove.count()-1+2);

        //COPY AND REMOVE FILES
        QThreadPool::globalInstance()->start([this, targetDir, sourceDir, diffirentFiles, filesToRemove](){
            if(fileHandler->copyFiles(sourceDir, targetDir, diffirentFiles))
            {
                if(!fileHandler->removeFiles(targetDir, filesToRemove))
                    qWarning()<<"Failed to remove some files!";

                emit fileHandler->copyFinished(true);
            }
            emit fileHandler->copyFinished(false);
        });
    }
}
