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
class QLabel;

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
    void onRecordMovie();
    void onPlayMovie();
    void onStopMovie();
    void onHardReset();
    void onYankCartridge();
    void onSwapCartridge();
    void onMovieEnded(const QString &summary, bool ok);

private:
    Ui::MainWindow *ui;
    EmulatorWidget *emulatorWidget;
    QLabel *fpsLabel;
    ZeroPoint::ROM rom;

    // Configuration settings
    int windowScale;
    bool vsyncEnabled;
    bool smoothFiltering;
    bool audioEnabled;
    int volume;
    QString biosPath;
    QString apuBiosPath;
    KeyBindings keyBindings;

    // Movie menu, built in code (see MainWindow::buildMovieMenu).
    QAction *actionRecordMovie = nullptr, *actionPlayMovie = nullptr, *actionStopMovie = nullptr;
    QAction *actionSoftReset = nullptr, *actionHardReset = nullptr, *actionYank = nullptr, *actionSwap = nullptr;
    QAction *actionTwoPlayers = nullptr;
    QString lastRomPath;   // where Play Movie looks for the ROMs a movie pins, besides the movie's own folder
    void buildMovieMenu();
    void updateMovieActions();

    void switchToEmulatorView();
    void applyWindowScale(int scale);
    void applySettingsToEmulator();
    void updateEmulationControls(bool active);
    void loadConfiguration();
    void saveConfiguration();
};

#endif // MAINWINDOW_H
