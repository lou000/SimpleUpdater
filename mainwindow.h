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
    MainWindow(std::optional<QDir> sourceLocation, std::optional<QDir> targetLocation,
               bool isInstall, QWidget *parent = nullptr);
    ~MainWindow();

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
    QPushButton* updateLaterButton;
    QPushButton* cancelButton;
    QPushButton* continueButton;
    QPushButton* quitButton;

    FileHandler* fileHandler;
    std::optional<QSettings> sourceInfo;
    std::optional<QSettings> targetInfo;

signals:
    void processFinished(bool success);

private slots:
    void logMessage(QString msg, QColor color);
    bool finalize(QDir appDirectory, QStringList args, bool makeShortcut = false);
};
#endif // MAINWINDOW_H
