#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include "rom.h"
#include "keybindings.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class EmulatorWidget;
class QAction;

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
    void onBootBios();
    void onConfiguration();
    void onScaleAction(QAction *action);
    void onToggleFullscreen(bool checked);
    void onToggleMenuBar(bool checked);
    void onEscapePressed();

private:
    Ui::MainWindow *ui;
    EmulatorWidget *emulatorWidget;
    ZeroPoint::ROM rom;

    // Configuration settings
    int windowScale;
    bool vsyncEnabled;
    bool smoothFiltering;
    bool audioEnabled;
    int volume;
    QString biosPath;
    KeyBindings keyBindings;

    void switchToEmulatorView();
    void applyWindowScale(int scale);
    void applySettingsToEmulator();
    void updateEmulationControls(bool active);
    void loadConfiguration();
    void saveConfiguration();
};

#endif // MAINWINDOW_H
