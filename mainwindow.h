#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
#include <QMainWindow>
#include <QSettings>

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
    MainWindow(const std::optional<QDir>& sourceLocation, const std::optional<QDir>& targetLocation, QWidget *parent = nullptr);

private:
    void installApplication(QDir sourceDir, QDir targetDir);
    void updateApplication(QDir sourceDir, QDir targetDir);

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
    QPushButton* continueButton;
    QPushButton* quitButton;

    FileHandler* fileHandler;
    std::optional<QSettings> sourceInfo;
    std::optional<QSettings> targetInfo;
};
#endif // MAINWINDOW_H
