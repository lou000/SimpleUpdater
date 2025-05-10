#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
#include <QFile>
#include <QMainWindow>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QDir *location, const QFile *originalApplication, bool fullUpdate, QWidget *parent = nullptr);
    ~MainWindow();
};
#endif // MAINWINDOW_H
