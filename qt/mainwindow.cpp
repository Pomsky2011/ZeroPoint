#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "configdialog.h"
#include "emulatorwidget.h"
#include "display.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>
#include <QActionGroup>
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

    if (rom.load(filename.toStdString())) {
        // ROM loaded successfully
        QString info = QString("Loaded: %1\nDeveloper: %2\nEntry Point: 0x%3")
            .arg(QString::fromStdString(rom.getTitle()))
            .arg(QString::fromStdString(rom.getDeveloper()))
            .arg(rom.getEntryPoint(), 8, 16, QChar('0'));

        ui->statusLabel->setText(info);
        ui->statusbar->showMessage(QString::fromStdString(rom.getTitle()), 3000);

        // Load ROM into the emulator widget's System - this actually drives
        // CPU/PPU/APU execution, not just ROM metadata.
        if (!emulatorWidget->loadROM(filename)) {
            QMessageBox::critical(this, tr("Load Error"), tr("Failed to load ROM into emulator"));
            return;
        }

        switchToEmulatorView();
        updateEmulationControls(true);
        onStart();

        QMessageBox::information(this, tr("ROM Loaded"), info);
    } else {
        QMessageBox::critical(
            this,
            tr("Load Error"),
            QString::fromStdString(rom.getError())
        );
    }
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
        emulatorWidget->stop();
        ui->actionStart->setEnabled(true);
        ui->actionStop->setEnabled(false);
        ui->statusbar->showMessage(tr("Emulation stopped"));
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
        QMessageBox::critical(this, tr("Boot Error"),
            tr("Failed to boot BIOS from:\n%1").arg(biosPath));
        return;
    }

    ui->statusLabel->setText(tr("BIOS booted (no cartridge)"));
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

    if (dialog.exec() == QDialog::Accepted) {
        int newScale = dialog.getWindowScale();
        vsyncEnabled = dialog.getVSync();
        smoothFiltering = dialog.getSmoothFiltering();
        audioEnabled = dialog.getAudioEnabled();
        volume = dialog.getVolume();
        keyBindings = dialog.getKeyBindings();
        biosPath = dialog.getBiosPath();

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
    emulatorWidget->setBootROMPath(biosPath);
}

void MainWindow::updateEmulationControls(bool active)
{
    ui->actionStart->setEnabled(active);
    ui->actionStop->setEnabled(false);
    ui->actionReset->setEnabled(active);
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
