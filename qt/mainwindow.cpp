#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "configdialog.h"
#include "emulatorwidget.h"
#include "serialconsole.h"
#include "display.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>
#include <QActionGroup>
#include <QDir>
#include <QShortcut>
#include <QKeySequence>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , emulatorWidget(nullptr)
    , windowScale(2)
    , vsyncEnabled(true)
    , smoothFiltering(false)
    , audioEnabled(true)
    , volume(80)
{
    ui->setupUi(this);

    // Menu actions are only added to the (hideable) menu bar's submenus in
    // the .ui file, so their default Qt::WindowShortcut context ties firing
    // to that submenu's visibility. Re-registering them on the window itself
    // keeps every shortcut - including Ctrl+M for actionShowMenuBar - live
    // even while the menu bar is hidden; otherwise hiding it once strands
    // the user with no keyboard way to bring it back.
    addAction(ui->actionLoadROM);
    addAction(ui->actionExit);
    addAction(ui->actionStart);
    addAction(ui->actionStop);
    addAction(ui->actionReset);
    addAction(ui->actionBootBios);
    addAction(ui->actionConfiguration);
    addAction(ui->actionScale1x);
    addAction(ui->actionScale2x);
    addAction(ui->actionScale3x);
    addAction(ui->actionScale4x);
    addAction(ui->actionFullscreen);
    addAction(ui->actionShowMenuBar);

    buildRecentMenu();

    // Connect menu actions
    connect(ui->actionLoadROM, &QAction::triggered, this, &MainWindow::onLoadROM);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    connect(ui->actionStart, &QAction::triggered, this, &MainWindow::onStart);
    connect(ui->actionStop, &QAction::triggered, this, &MainWindow::onStop);
    connect(ui->actionReset, &QAction::triggered, this, &MainWindow::onReset);
    connect(ui->actionBootBios, &QAction::triggered, this, &MainWindow::onBootBios);
    connect(ui->actionConfiguration, &QAction::triggered, this, &MainWindow::onConfiguration);

    QActionGroup *scaleGroup = new QActionGroup(this);
    scaleGroup->setExclusive(true);
    scaleGroup->addAction(ui->actionScale1x);
    scaleGroup->addAction(ui->actionScale2x);
    scaleGroup->addAction(ui->actionScale3x);
    scaleGroup->addAction(ui->actionScale4x);
    connect(ui->menuWindowScale, &QMenu::triggered, this, &MainWindow::onScaleAction);

    connect(ui->actionFullscreen, &QAction::toggled, this, &MainWindow::onToggleFullscreen);
    connect(ui->actionShowMenuBar, &QAction::toggled, this, &MainWindow::onToggleMenuBar);

    // Escape backs out of fullscreen even though it isn't a QAction shortcut
    // (binding it to one would also let Escape *enter* fullscreen).
    QShortcut *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &MainWindow::onEscapePressed);

    // Create emulator widget (initially hidden)
    emulatorWidget = new EmulatorWidget(this);
    emulatorWidget->hide();
    buildMovieMenu();
    connect(emulatorWidget, &EmulatorWidget::movieEnded, this, &MainWindow::onMovieEnded);

    // Bottom-right of the status bar. Empty while stopped.
    fpsLabel = new QLabel(this);
    ui->statusbar->addPermanentWidget(fpsLabel);
    connect(emulatorWidget, &EmulatorWidget::fpsChanged, this, [this](int fps) {
        fpsLabel->setText(fps > 0 ? tr("%1 FPS").arg(fps) : QString());
    });

    // Load saved configuration
    loadConfiguration();
    applySettingsToEmulator();

    QAction *scaleActions[] = {ui->actionScale1x, ui->actionScale2x, ui->actionScale3x, ui->actionScale4x};
    int scaleIndex = qBound(1, windowScale, 4) - 1;
    scaleActions[scaleIndex]->setChecked(true);

    // Set initial window size
    resize(800, 600);
}

MainWindow::~MainWindow()
{
    saveConfiguration();
    delete ui;
}

void MainWindow::onLoadROM()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Load ROM"),
        QString(),
        tr("ZeroPoint Binary (*.zpb);;All Files (*)")
    );

    if (filename.isEmpty()) {
        return;
    }

    openROMFile(filename, true);
}

void MainWindow::openROMFile(const QString &filename, bool announce)
{
    if (rom.load(filename.toStdString())) {
        // ROM loaded successfully
        QString info = QString("Loaded: %1\nDeveloper: %2\nEntry Point: 0x%3")
            .arg(QString::fromStdString(rom.getTitle()))
            .arg(QString::fromStdString(rom.getDeveloper()))
            .arg(rom.getEntryPoint(), 8, 16, QChar('0'));

        setWelcomeText(info);
        ui->statusbar->showMessage(QString::fromStdString(rom.getTitle()), 3000);

        // Load ROM into the emulator widget's System - this actually drives
        // CPU/PPU/APU execution, not just ROM metadata.
        lastRomPath = filename;
        if (!emulatorWidget->loadROM(filename)) {
            updateEmulationControls(false);   // the failed load tore the old machine down too
            QMessageBox::critical(this, tr("Load Error"), tr("Failed to load ROM into emulator"));
            return;
        }

        addRecentRom(filename, QString::fromStdString(rom.getTitle()));
        switchToEmulatorView();
        updateEmulationControls(true);
        onStart();

        if (announce) {
            QMessageBox::information(this, tr("ROM Loaded"), info);
        }
    } else {
        QMessageBox::critical(
            this,
            tr("Load Error"),
            QString::fromStdString(rom.getError())
        );
    }
}

void MainWindow::buildRecentMenu()
{
    // Sits right after File. The entries are rebuilt each time it opens so a file that went away (or came back, e.g. a
    // removable drive) shows up greyed out (or live) without a restart.
    menuRecent = new QMenu(tr("&Recent"), this);
    ui->menubar->insertMenu(ui->menuEmulation->menuAction(), menuRecent);
    connect(menuRecent, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    menuRecent->clear();
    for (const RecentRom &r : recentRoms) {
        const QString label = r.title.isEmpty() ? QFileInfo(r.path).fileName() : r.title;
        QAction *a = menuRecent->addAction(label);
        a->setToolTip(r.path);
        a->setStatusTip(r.path);
        a->setEnabled(QFileInfo::exists(r.path));
        const QString path = r.path;
        connect(a, &QAction::triggered, this, [this, path]() { openROMFile(path, false); });
    }
    if (recentRoms.isEmpty()) {
        menuRecent->addAction(tr("(no recent ROMs)"))->setEnabled(false);
    } else {
        menuRecent->addSeparator();
        QAction *clear = menuRecent->addAction(tr("&Clear Recent ROMs"));
        connect(clear, &QAction::triggered, this, [this]() {
            recentRoms.clear();
            saveRecentRoms();
            rebuildRecentMenu();
        });
    }
}

void MainWindow::addRecentRom(const QString &path, const QString &title)
{
    const QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (int i = 0; i < recentRoms.size(); ++i)
        if (recentRoms[i].path == abs) recentRoms.removeAt(i--);
    recentRoms.prepend({abs, title});
    while (recentRoms.size() > MAX_RECENT) recentRoms.removeLast();
    saveRecentRoms();   // now, not at exit, so a crash doesn't lose it
    rebuildRecentMenu();
}

void MainWindow::loadRecentRoms()
{
    QSettings settings("ZeroPoint", "Emulator");
    recentRoms.clear();
    const int n = qMin(settings.beginReadArray("recent"), int(MAX_RECENT));
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        const QString path = settings.value("path").toString();
        if (!path.isEmpty()) recentRoms.append({path, settings.value("title").toString()});
    }
    settings.endArray();
}

void MainWindow::saveRecentRoms()
{
    QSettings settings("ZeroPoint", "Emulator");
    settings.remove("recent");
    settings.beginWriteArray("recent", recentRoms.size());
    for (int i = 0; i < recentRoms.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("path", recentRoms[i].path);
        settings.setValue("title", recentRoms[i].title);
    }
    settings.endArray();
}

void MainWindow::buildMovieMenu()
{
    // Built here rather than in the .ui form: movies, the reset buttons and the cartridge slot are all "things a player
    // does to the console", and they share one menu. F1-F4 match the SDL frontend.
    QMenu *menu = ui->menubar->addMenu(tr("&Console"));
    actionSoftReset = menu->addAction(tr("&Soft Reset"));
    actionHardReset = menu->addAction(tr("&Hard Reset (Power Cycle)"));
    actionYank = menu->addAction(tr("&Yank Cartridge"));
    actionSwap = menu->addAction(tr("Stop 'n' S&wop Cartridge..."));
    menu->addSeparator();
    actionTwoPlayers = menu->addAction(tr("&Two Players (player 2: i j k l, 7 8 9 0, u o n m)"));
    actionTwoPlayers->setCheckable(true);
    menu->addSeparator();
    actionDevUnit = menu->addAction(tr("&Dev Unit (debug serial port; signature checks always pass)"));
    actionDevUnit->setCheckable(true);
    actionSerialConsole = menu->addAction(tr("Serial &Console..."));
    menu->addSeparator();
    actionRecordMovie = menu->addAction(tr("&Record Movie..."));
    actionPlayMovie = menu->addAction(tr("&Play Movie..."));
    actionStopMovie = menu->addAction(tr("S&top Movie"));

    actionSoftReset->setShortcut(QKeySequence(Qt::Key_F1));
    actionHardReset->setShortcut(QKeySequence(Qt::Key_F2));
    actionYank->setShortcut(QKeySequence(Qt::Key_F3));
    actionSwap->setShortcut(QKeySequence(Qt::Key_F4));
    for (QAction *a : {actionSoftReset, actionHardReset, actionYank, actionSwap, actionTwoPlayers, actionDevUnit,
                       actionSerialConsole, actionRecordMovie, actionPlayMovie, actionStopMovie})
        addAction(a);   // live even with the menu bar hidden (see the constructor)

    connect(actionSoftReset, &QAction::triggered, this, &MainWindow::onReset);
    connect(actionHardReset, &QAction::triggered, this, &MainWindow::onHardReset);
    connect(actionYank, &QAction::triggered, this, &MainWindow::onYankCartridge);
    connect(actionSwap, &QAction::triggered, this, &MainWindow::onSwapCartridge);
    connect(actionTwoPlayers, &QAction::toggled, this, [this](bool on) { emulatorWidget->setTwoPlayers(on); });
    connect(actionDevUnit, &QAction::toggled, this, [this](bool on) {
        emulatorWidget->setDevUnit(on);
        if (emulatorWidget->isActive())
            ui->statusbar->showMessage(tr("Dev unit %1: takes effect when a cartridge is next loaded").arg(on ? tr("on") : tr("off")), 6000);
    });
    connect(actionSerialConsole, &QAction::triggered, this, [this]() {
        serialConsole->show();
        serialConsole->raise();
        serialConsole->activateWindow();
    });
    serialConsole = new SerialConsole(this);
    connect(emulatorWidget, &EmulatorWidget::serialOutput, serialConsole, &SerialConsole::appendOutput);
    connect(serialConsole, &SerialConsole::lineEntered, emulatorWidget, &EmulatorWidget::serialSend);
    connect(emulatorWidget, &EmulatorWidget::serialSetupFailed, this, [this](const QString &message) {
        ui->statusbar->showMessage(tr("Serial port: %1").arg(message), 10000);
    });
    connect(actionRecordMovie, &QAction::triggered, this, &MainWindow::onRecordMovie);
    connect(actionPlayMovie, &QAction::triggered, this, &MainWindow::onPlayMovie);
    connect(actionStopMovie, &QAction::triggered, this, &MainWindow::onStopMovie);
    updateMovieActions();
}

void MainWindow::setSerialOptions(bool devUnit, const QStringList &outs, const QStringList &ins, int tcpPort, const QString &exceptionHandler)
{
    emulatorWidget->setSerialEndpoints(outs, ins, tcpPort);
    emulatorWidget->setExceptionHandler(exceptionHandler);
    actionDevUnit->setChecked(devUnit);   // also tells the widget
    if (devUnit) serialConsole->show();
}

void MainWindow::updateMovieActions()
{
    const bool active = emulatorWidget && emulatorWidget->isActive();
    const bool movie = emulatorWidget && (emulatorWidget->isRecording() || emulatorWidget->isPlayingMovie());
    actionSoftReset->setEnabled(active && !emulatorWidget->isPlayingMovie());
    actionHardReset->setEnabled(active && !emulatorWidget->isPlayingMovie());
    actionYank->setEnabled(active && !emulatorWidget->isPlayingMovie());
    actionSwap->setEnabled(active && !emulatorWidget->isPlayingMovie());
    actionTwoPlayers->setEnabled(!emulatorWidget || !emulatorWidget->isPlayingMovie());
    actionRecordMovie->setEnabled(active && !movie);
    actionPlayMovie->setEnabled(!movie);
    actionStopMovie->setEnabled(movie);
}

void MainWindow::onHardReset()
{
    if (emulatorWidget && emulatorWidget->isActive()) {
        emulatorWidget->hardReset();
        ui->statusbar->showMessage(tr("Hard reset (power cycle)"), 3000);
    }
}

void MainWindow::onYankCartridge()
{
    if (emulatorWidget && emulatorWidget->isActive()) {
        emulatorWidget->yankCartridge();
        ui->statusbar->showMessage(tr("Cartridge yanked"), 3000);
    }
}

void MainWindow::onSwapCartridge()
{
    if (!emulatorWidget || !emulatorWidget->isActive()) return;
    const QString filename = QFileDialog::getOpenFileName(this, tr("Stop 'n' Swop: insert cartridge"), lastRomPath,
                                                          tr("ZeroPoint Binary (*.zpb);;All Files (*)"));
    if (filename.isEmpty()) return;
    QString error;
    if (!emulatorWidget->swapCartridge(filename, &error)) {
        QMessageBox::critical(this, tr("Swap Error"), error);
        return;
    }
    lastRomPath = filename;
    ui->statusbar->showMessage(tr("Swapped in %1").arg(QFileInfo(filename).fileName()), 3000);
}

void MainWindow::onRecordMovie()
{
    const QString filename = QFileDialog::getSaveFileName(this, tr("Record Movie"), QString(), tr("ZeroPoint Movie (*.zpm)"));
    if (filename.isEmpty()) return;
    QString error;
    if (!emulatorWidget->startRecording(filename, QString(), &error)) {
        updateEmulationControls(false);
        QMessageBox::critical(this, tr("Record Error"), error);
        return;
    }
    switchToEmulatorView();
    updateEmulationControls(true);
    onStart();
    updateMovieActions();
    ui->statusbar->showMessage(tr("Recording from power-on to %1 (Console > Stop Movie to save)").arg(QFileInfo(filename).fileName()));
}

void MainWindow::onPlayMovie()
{
    const QString filename = QFileDialog::getOpenFileName(this, tr("Play Movie"), QString(), tr("ZeroPoint Movie (*.zpm)"));
    if (filename.isEmpty()) return;
    QStringList roms;
    if (!lastRomPath.isEmpty()) roms << QFileInfo(lastRomPath).absolutePath();
    QString error;
    if (!emulatorWidget->startPlayback(filename, roms, &error)) {
        updateEmulationControls(emulatorWidget->isActive());
        QMessageBox::critical(this, tr("Play Error"), error);
        return;
    }
    switchToEmulatorView();
    updateEmulationControls(true);
    onStart();
    updateMovieActions();
    ui->statusbar->showMessage(tr("Playing %1").arg(QFileInfo(filename).fileName()));
}

void MainWindow::onStopMovie()
{
    const QString summary = emulatorWidget->finishMovie();
    updateEmulationControls(false);   // the machine is gone
    updateMovieActions();
    if (!summary.isEmpty()) ui->statusbar->showMessage(summary, 8000);
}

void MainWindow::onMovieEnded(const QString &summary, bool ok)
{
    emulatorWidget->finishMovie();
    updateEmulationControls(false);
    updateMovieActions();
    ui->statusbar->showMessage(summary, 8000);
    if (!ok) QMessageBox::warning(this, tr("Movie"), summary);
}

void MainWindow::onExit()
{
    close();
}

void MainWindow::onStart()
{
    if (emulatorWidget && emulatorWidget->isActive()) {
        emulatorWidget->start();
        ui->actionStart->setEnabled(false);
        ui->actionStop->setEnabled(true);
        ui->statusbar->showMessage(tr("Emulation started"));
    }
}

void MainWindow::onStop()
{
    if (emulatorWidget) {
        // Stopping tears the System down. A movie in progress is finished (a recording saved) first.
        QString summary;
        if (emulatorWidget->isRecording() || emulatorWidget->isPlayingMovie()) {
            summary = emulatorWidget->finishMovie();
        } else {
            emulatorWidget->stop();
        }
        updateEmulationControls(false);   // nothing to start again: load a ROM for a new machine
        ui->statusbar->showMessage(summary.isEmpty() ? tr("Emulation stopped") : summary);
    }
}

void MainWindow::onReset()
{
    if (emulatorWidget && emulatorWidget->isActive()) {
        emulatorWidget->reset();
        ui->statusbar->showMessage(tr("Emulation reset"));
    }
}

void MainWindow::onBootBios()
{
    if (!emulatorWidget->bootBIOS()) {
        updateEmulationControls(false);
        QMessageBox::critical(this, tr("Boot Error"),
            tr("Failed to boot BIOS from:\n%1").arg(biosPath));
        return;
    }

    setWelcomeText(tr("BIOS booted (no cartridge)"));
    ui->statusbar->showMessage(tr("BIOS booted"), 3000);

    switchToEmulatorView();
    updateEmulationControls(true);
    onStart();
}

void MainWindow::onConfiguration()
{
    ConfigDialog dialog(this);
    dialog.setWindowScale(windowScale);
    dialog.setVSync(vsyncEnabled);
    dialog.setSmoothFiltering(smoothFiltering);
    dialog.setAudioEnabled(audioEnabled);
    dialog.setVolume(volume);
    dialog.setKeyBindings(keyBindings);
    dialog.setBiosPath(biosPath);
    dialog.setApuBiosPath(apuBiosPath);

    if (dialog.exec() == QDialog::Accepted) {
        int newScale = dialog.getWindowScale();
        vsyncEnabled = dialog.getVSync();
        smoothFiltering = dialog.getSmoothFiltering();
        audioEnabled = dialog.getAudioEnabled();
        volume = dialog.getVolume();
        keyBindings = dialog.getKeyBindings();
        biosPath = dialog.getBiosPath();
        apuBiosPath = dialog.getApuBiosPath();

        saveConfiguration();
        applySettingsToEmulator();

        if (newScale != windowScale) {
            windowScale = newScale;
            QAction *scaleActions[] = {ui->actionScale1x, ui->actionScale2x, ui->actionScale3x, ui->actionScale4x};
            scaleActions[qBound(1, windowScale, 4) - 1]->setChecked(true);
            applyWindowScale(windowScale);
        }

        ui->statusbar->showMessage(tr("Configuration updated"), 3000);
    }
}

void MainWindow::onScaleAction(QAction *action)
{
    int scale = 2;
    if (action == ui->actionScale1x) scale = 1;
    else if (action == ui->actionScale2x) scale = 2;
    else if (action == ui->actionScale3x) scale = 3;
    else if (action == ui->actionScale4x) scale = 4;
    else return;

    windowScale = scale;
    saveConfiguration();
    applyWindowScale(windowScale);
}

void MainWindow::onToggleFullscreen(bool checked)
{
    if (checked) {
        ui->menubar->setVisible(false);
        ui->statusbar->setVisible(false);
        showFullScreen();
    } else {
        showNormal();
        ui->menubar->setVisible(ui->actionShowMenuBar->isChecked());
        ui->statusbar->setVisible(true);
        applyWindowScale(windowScale);
    }
}

void MainWindow::onToggleMenuBar(bool checked)
{
    // While fullscreen the menu bar stays hidden regardless - this just
    // records what to restore once fullscreen is exited.
    if (!ui->actionFullscreen->isChecked()) {
        ui->menubar->setVisible(checked);
    }
}

void MainWindow::onEscapePressed()
{
    if (ui->actionFullscreen->isChecked()) {
        ui->actionFullscreen->setChecked(false);
    }
}

void MainWindow::switchToEmulatorView()
{
    if (centralWidget() != emulatorWidget) {
        QWidget *oldWidget = centralWidget();
        setCentralWidget(emulatorWidget);
        emulatorWidget->show();
        delete oldWidget;
        applyWindowScale(windowScale);
    }
}

void MainWindow::setWelcomeText(const QString &text)
{
    if (centralWidget() != emulatorWidget) {
        ui->statusLabel->setText(text);
    }
}

void MainWindow::applyWindowScale(int scale)
{
    if (isFullScreen()) {
        return;
    }
    int menuBarHeight = ui->menubar->isVisible() ? ui->menubar->height() : 0;
    resize(ZeroPoint::FB_WIDTH * scale, ZeroPoint::FULL_HEIGHT * scale + menuBarHeight);
}

void MainWindow::applySettingsToEmulator()
{
    if (!emulatorWidget) {
        return;
    }
    emulatorWidget->setKeyBindings(keyBindings);
    emulatorWidget->setSmoothFiltering(smoothFiltering);
    emulatorWidget->setAudioEnabled(audioEnabled);
    emulatorWidget->setAudioVolume(volume);
    emulatorWidget->setBootROMPath(biosPath);
    emulatorWidget->setApuBiosPath(apuBiosPath);
}

void MainWindow::updateEmulationControls(bool active)
{
    ui->actionStart->setEnabled(active);
    ui->actionStop->setEnabled(false);
    ui->actionReset->setEnabled(active);
    if (actionRecordMovie) updateMovieActions();
}

void MainWindow::loadConfiguration()
{
    QSettings settings("ZeroPoint", "Emulator");
    windowScale = settings.value("display/scale", 2).toInt();
    vsyncEnabled = settings.value("display/vsync", true).toBool();
    smoothFiltering = settings.value("display/smoothFiltering", false).toBool();
    audioEnabled = settings.value("audio/enabled", true).toBool();
    volume = settings.value("audio/volume", 80).toInt();
    biosPath = settings.value("system/biosPath", QString()).toString();
    apuBiosPath = settings.value("system/apuBiosPath", QString()).toString();
    loadRecentRoms();
    rebuildRecentMenu();

    KeyBindings defaults;
    keyBindings.up = settings.value("input/up", defaults.up).toInt();
    keyBindings.down = settings.value("input/down", defaults.down).toInt();
    keyBindings.left = settings.value("input/left", defaults.left).toInt();
    keyBindings.right = settings.value("input/right", defaults.right).toInt();
    keyBindings.button1 = settings.value("input/button1", defaults.button1).toInt();
    keyBindings.button2 = settings.value("input/button2", defaults.button2).toInt();
    keyBindings.button3 = settings.value("input/button3", defaults.button3).toInt();
    keyBindings.button4 = settings.value("input/button4", defaults.button4).toInt();
    keyBindings.bigLeft = settings.value("input/bigLeft", defaults.bigLeft).toInt();
    keyBindings.littleLeft = settings.value("input/littleLeft", defaults.littleLeft).toInt();
    keyBindings.littleRight = settings.value("input/littleRight", defaults.littleRight).toInt();
    keyBindings.bigRight = settings.value("input/bigRight", defaults.bigRight).toInt();
    keyBindings.menu = settings.value("input/menu", defaults.menu).toInt();
    keyBindings.pause = settings.value("input/pause", defaults.pause).toInt();
}

void MainWindow::saveConfiguration()
{
    QSettings settings("ZeroPoint", "Emulator");
    settings.setValue("display/scale", windowScale);
    settings.setValue("display/vsync", vsyncEnabled);
    settings.setValue("display/smoothFiltering", smoothFiltering);
    settings.setValue("audio/enabled", audioEnabled);
    settings.setValue("audio/volume", volume);
    settings.setValue("system/biosPath", biosPath);
    settings.setValue("system/apuBiosPath", apuBiosPath);

    settings.setValue("input/up", keyBindings.up);
    settings.setValue("input/down", keyBindings.down);
    settings.setValue("input/left", keyBindings.left);
    settings.setValue("input/right", keyBindings.right);
    settings.setValue("input/button1", keyBindings.button1);
    settings.setValue("input/button2", keyBindings.button2);
    settings.setValue("input/button3", keyBindings.button3);
    settings.setValue("input/button4", keyBindings.button4);
    settings.setValue("input/bigLeft", keyBindings.bigLeft);
    settings.setValue("input/littleLeft", keyBindings.littleLeft);
    settings.setValue("input/littleRight", keyBindings.littleRight);
    settings.setValue("input/bigRight", keyBindings.bigRight);
    settings.setValue("input/menu", keyBindings.menu);
    settings.setValue("input/pause", keyBindings.pause);
}
