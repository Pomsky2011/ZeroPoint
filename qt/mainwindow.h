#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QList>
#include "rom.h"
#include "keybindings.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class EmulatorWidget;
class SerialConsole;
class QAction;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Load a .zpb and start it, as File > Load ROM does. `announce` = also show the "ROM Loaded" box (the menu does,
    // a ROM handed to us on the command line / by the desktop does not).
    void openROMFile(const QString &filename, bool announce);

    // The command line's --dev-unit / --serial-out / --serial-in / --serial-tcp / --exception-handler (same as zeropoint_sdl). Call before loading.
    void setSerialOptions(bool devUnit, const QStringList &outs, const QStringList &ins, int tcpPort, const QString &exceptionHandler);

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
    QAction *actionDevUnit = nullptr, *actionSerialConsole = nullptr;
    SerialConsole *serialConsole = nullptr;
    QString lastRomPath;   // where Play Movie looks for the ROMs a movie pins, besides the movie's own folder
    void buildMovieMenu();
    void updateMovieActions();

    // Recent ROMs: a top-level menu, most recent first, kept in QSettings ("recent/").
    struct RecentRom { QString path, title; };
    static constexpr int MAX_RECENT = 10;
    QMenu *menuRecent = nullptr;
    QList<RecentRom> recentRoms;
    void buildRecentMenu();
    void rebuildRecentMenu();      // also re-checks which files still exist
    void addRecentRom(const QString &path, const QString &title);
    void loadRecentRoms();
    void saveRecentRoms();

    void switchToEmulatorView();
    // The welcome page's label dies with the page (switchToEmulatorView() deletes it), so only touch it before then.
    void setWelcomeText(const QString &text);
    void applyWindowScale(int scale);
    void applySettingsToEmulator();
    void updateEmulationControls(bool active);
    void loadConfiguration();
    void saveConfiguration();
};

#endif // MAINWINDOW_H
