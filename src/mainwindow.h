#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cliparser.h"
#include <QMainWindow>

class QPushButton;
class QLineEdit;
class QLabel;
class UpdateController;
class QStackedWidget;
class QTextEdit;
class QProgressBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const CliResult& config, QWidget* parent = nullptr);
    ~MainWindow();

private:
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
    QLabel* headerIcon;
    QLabel* headerTitle;
    QLabel* headerSubtitle;

    UpdateController* m_controller;

    static QString exeDisplayName(const QString& appExe);

private slots:
    void logMessage(const QString& msg, const QColor& color);
};

#endif // MAINWINDOW_H
