#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include "rom.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class EmulatorWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoadROM();
    void onExit();
    void onStart();
    void onStop();
    void onReset();
    void onConfiguration();

private:
    Ui::MainWindow *ui;
    EmulatorWidget *emulatorWidget;
    ZeroPoint::ROM rom;

    // Configuration settings
    int windowScale;
    bool vsyncEnabled;
    bool audioEnabled;
    int volume;

    void updateEmulationControls(bool romLoaded);
    void loadConfiguration();
    void saveConfiguration();
};

#endif // MAINWINDOW_H
