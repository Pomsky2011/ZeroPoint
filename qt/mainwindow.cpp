#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "configdialog.h"
#include "emulatorwidget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , emulatorWidget(nullptr)
    , windowScale(2)
    , vsyncEnabled(true)
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
    connect(ui->actionConfiguration, &QAction::triggered, this, &MainWindow::onConfiguration);

    // Create emulator widget (initially hidden)
    emulatorWidget = new EmulatorWidget(this);
    emulatorWidget->hide();

    // Load saved configuration
    loadConfiguration();

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

        // Load ROM into emulator widget
        emulatorWidget->loadROM(&rom);

        // Replace central widget with emulator
        QWidget *oldWidget = ui->centralwidget;
        setCentralWidget(emulatorWidget);
        emulatorWidget->show();
        delete oldWidget;

        // Adjust window size for emulator
        resize(256 * windowScale, 256 * windowScale + ui->menubar->height());

        updateEmulationControls(true);

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
    if (emulatorWidget && rom.isLoaded()) {
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
    if (emulatorWidget && rom.isLoaded()) {
        emulatorWidget->reset();
        ui->statusbar->showMessage(tr("Emulation reset"));
    }
}

void MainWindow::onConfiguration()
{
    ConfigDialog dialog(this);
    dialog.setWindowScale(windowScale);
    dialog.setVSync(vsyncEnabled);
    dialog.setAudioEnabled(audioEnabled);
    dialog.setVolume(volume);

    if (dialog.exec() == QDialog::Accepted) {
        windowScale = dialog.getWindowScale();
        vsyncEnabled = dialog.getVSync();
        audioEnabled = dialog.getAudioEnabled();
        volume = dialog.getVolume();

        saveConfiguration();

        // Apply settings to emulator widget if loaded
        if (emulatorWidget && rom.isLoaded()) {
            // Would apply settings here when implemented
            ui->statusbar->showMessage(tr("Configuration updated"), 3000);
        }
    }
}

void MainWindow::updateEmulationControls(bool romLoaded)
{
    ui->actionStart->setEnabled(romLoaded);
    ui->actionStop->setEnabled(false);
    ui->actionReset->setEnabled(romLoaded);
}

void MainWindow::loadConfiguration()
{
    QSettings settings("ZeroPoint", "Emulator");
    windowScale = settings.value("display/scale", 2).toInt();
    vsyncEnabled = settings.value("display/vsync", true).toBool();
    audioEnabled = settings.value("audio/enabled", true).toBool();
    volume = settings.value("audio/volume", 80).toInt();
}

void MainWindow::saveConfiguration()
{
    QSettings settings("ZeroPoint", "Emulator");
    settings.setValue("display/scale", windowScale);
    settings.setValue("display/vsync", vsyncEnabled);
    settings.setValue("audio/enabled", audioEnabled);
    settings.setValue("audio/volume", volume);
}
