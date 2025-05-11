#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
#include <QMainWindow>

class QPushButton;
class QLineEdit;
class FileHandler;
class QStackedWidget;
class QTextEdit;
class QProgressBar;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QDir& sourceLocation, const QDir& targetLocation, const QString &originalApplication,
               bool fullUpdate, bool installation, QWidget *parent = nullptr);

private:
    void installApplication(const QDir &dir);
    void updateApplication();

    QStackedWidget* stackedWidget;
    QWidget* installScreen;
    QWidget* updateScreen;
    QWidget* progressScreen;
    QLineEdit* pathEdit;
    QPushButton* browseButton;
    QProgressBar* progressBar;
    QTextEdit* logBox;
    QPushButton* updateLater;
    QPushButton* cancelButton;
    QPushButton* proceedButton;

    FileHandler* fileHandler;
    QDir sourceLocation;
    QDir targetLocation;
    QString originalApplication;
    bool fullUpdate;
    bool forcedUpdate;
};
#endif // MAINWINDOW_H
