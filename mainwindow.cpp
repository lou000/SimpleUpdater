#include "mainwindow.h"

#include "filecopier.h"

#include <QSettings>

MainWindow::MainWindow(const QDir* location, const QFile* originalApplication, bool fullUpdate, QWidget *parent)
    :QMainWindow(parent)
{
    //original application can be invalid, location is a directory
    auto uiFile = QFile(location->filePath("update_info.ini"));
    QSettings* updateInfo = nullptr;
    if(uiFile.exists() && uiFile.isReadable())
        updateInfo = new QSettings(location->filePath("update_info.ini"), QSettings::Format::IniFormat);
    fileCopier = new FileCopier(this);

    //TODO: UI
}

MainWindow::~MainWindow() {}
