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

MainWindow::MainWindow(const QDir& sourceLocation, const QDir& targetLocation, const QString& originalApplication,
                       bool fullUpdate, bool installation, QWidget *parent)
    : QMainWindow(parent), sourceLocation(sourceLocation), targetLocation(targetLocation),
    originalApplication(originalApplication), fullUpdate(fullUpdate), fileHandler(new FileHandler(this))
{
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
    proceedButton = new QPushButton(tr("Proceed"));
    proceedButton->setDefault(true);
    buttonLayout->addWidget(updateLater);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(proceedButton);
    updateLater->setVisible(false);

    mainLayout->addLayout(buttonLayout);

    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    if(installation)
        stackedWidget->setCurrentIndex(0);
    else
        stackedWidget->setCurrentIndex(1);

    connect(browseButton, &QPushButton::clicked, this, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory");
        if (!dir.isEmpty())
            pathEdit->setText(dir);
    });

    connect(proceedButton, &QPushButton::clicked, this, [this, installation](){
        if(installation)
        {
            auto installationDir = QDir(pathEdit->text());
            if(!installationDir.mkpath(pathEdit->text()))
            {
                QMessageBox::critical(this, "Invalid directory",
                                      "The provided directory cannot be created or is inaccesible, check permissions.");
                return;
            }
            qDebug()<<installationDir;
            installApplication(installationDir);
        }
        else
            updateApplication();

        stackedWidget->setCurrentIndex(2);
        proceedButton->setVisible(false);
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

        QString resultStr = results.second ? "OK" : "ERROR";
        cursor.insertText(results.first+"    "+resultStr+"\n", format);
        if (atBottom)
            vScroll->setValue(vScroll->maximum());
    });
    connect(fileHandler, &FileHandler::copyFinished, this, [this](bool success){
        if(!success)
            QMessageBox::critical(this, tr("Copy failed!"),
                                  tr("Installation/update process failed, "
                                  "please refer to log to see what files were not possible to copy succesfully."));
        else
        {
            auto app = this->originalApplication;
            if(!app.isEmpty())
            {
                bool success = QProcess::startDetached(this->originalApplication, {});
                if(!success)
                    qWarning() << "Failed to start"<<this->originalApplication;
            }
            QThread::msleep(500);
            QApplication::quit();
        }
    });
    connect(fileHandler, &FileHandler::cancelled, this, [this](){
        QApplication::quit();
    });
}

void MainWindow::installApplication(const QDir& dir)
{
    int fileCount = 0;
    auto info = QSettings("updateInfo.ini", QSettings::IniFormat);
    if(info.contains("SETTINGS/file_count"))
        fileCount = info.value("SETTINGS/file_count").toInt();
    if(fileCount == 0)
        fileCount = FileHandler::getNumberOfFilesRecursive(QApplication::applicationDirPath());

    progressBar->setRange(0, fileCount-1);

    QThreadPool::globalInstance()->start([this, dir](){
        fileHandler->copyDirectoryRecursively(QApplication::applicationDirPath(), dir);
    });
}

void MainWindow::updateApplication()
{
    auto sourceInfo = QSettings(sourceLocation.filePath("updateInfo.ini"), QSettings::IniFormat);
    auto targetInfo = QSettings(targetLocation.filePath("updateInfo.ini"), QSettings::IniFormat);
    if(this->fullUpdate)
    {

    }
}
