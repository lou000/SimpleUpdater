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

MainWindow::MainWindow(const QDir& sourceLocation, const QDir& targetLocation, QFile* originalApplication,
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
    progressLayout->addWidget(progressBar);

    logBox = new QPlainTextEdit();
    logBox->setReadOnly(true);
    logBox->setMaximumBlockCount(1000);
    logBox->setMinimumHeight(150);
    progressLayout->addWidget(logBox);

    stackedWidget->addWidget(progressScreen);
    mainLayout->addWidget(stackedWidget, 1);

    // --------- Shared Buttons ---------
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    cancelButton = new QPushButton(tr("Cancel"));
    proceedButton = new QPushButton(tr("Proceed"));
    proceedButton->setDefault(true);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(proceedButton);

    mainLayout->addLayout(buttonLayout);

    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    connect(proceedButton, &QPushButton::clicked, this, [this](){
        int currentIndex = stackedWidget->currentIndex();

        // Switch between screens for debugging
        if (currentIndex == 0) {
            stackedWidget->setCurrentIndex(1);  // Go to Update screen
        } else if (currentIndex == 1) {
            stackedWidget->setCurrentIndex(2);  // Go to Progress screen
        } else {
            stackedWidget->setCurrentIndex(0);  // Go to Install screen
        }
    });
}

bool MainWindow::installApplication()
{
    return true;
}

bool MainWindow::updateApplication()
{
    return true;
}
