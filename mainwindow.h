#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
#include <QMainWindow>

class QPushButton;
class QLineEdit;
class FileHandler;
class QStackedWidget;
class QPlainTextEdit;
class QProgressBar;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QDir& sourceLocation, const QDir& targetLocation, QFile* originalApplication,
               bool fullUpdate, bool installation, QWidget *parent = nullptr);

private:
    bool installApplication();
    bool updateApplication();

    QStackedWidget* stackedWidget;
    QWidget* installScreen;
    QWidget* updateScreen;
    QWidget* progressScreen;
    QLineEdit* pathEdit;
    QPushButton* browseButton;
    QProgressBar* progressBar;
    QPlainTextEdit* logBox;
    QPushButton* cancelButton;
    QPushButton* proceedButton;

    FileHandler* fileHandler;
    QDir sourceLocation;
    QDir targetLocation;
    QFile* originalApplication;
    bool fullUpdate;
    bool forcedUpdate;
};
#endif // MAINWINDOW_H
