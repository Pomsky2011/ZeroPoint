#include "configdialog.h"
#include "ui_configdialog.h"
#include "keycapturebutton.h"
#include <QFileDialog>

ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigDialog)
{
    ui->setupUi(this);

    connect(ui->resetKeysButton, &QPushButton::clicked, this, &ConfigDialog::onResetKeys);
    connect(ui->biosBrowseButton, &QPushButton::clicked, this, &ConfigDialog::onBrowseBios);
    connect(ui->biosClearButton, &QPushButton::clicked, this, &ConfigDialog::onClearBios);
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
}

void ConfigDialog::setWindowScale(int scale)
{
    ui->scaleSpinBox->setValue(scale);
}

int ConfigDialog::getWindowScale() const
{
    return ui->scaleSpinBox->value();
}

void ConfigDialog::setVSync(bool enabled)
{
    ui->vsyncCheckBox->setChecked(enabled);
}

bool ConfigDialog::getVSync() const
{
    return ui->vsyncCheckBox->isChecked();
}

void ConfigDialog::setSmoothFiltering(bool smooth)
{
    ui->filterComboBox->setCurrentIndex(smooth ? 1 : 0);
}

bool ConfigDialog::getSmoothFiltering() const
{
    return ui->filterComboBox->currentIndex() == 1;
}

void ConfigDialog::setAudioEnabled(bool enabled)
{
    ui->enableAudioCheckBox->setChecked(enabled);
}

bool ConfigDialog::getAudioEnabled() const
{
    return ui->enableAudioCheckBox->isChecked();
}

void ConfigDialog::setVolume(int volume)
{
    ui->volumeSlider->setValue(volume);
}

int ConfigDialog::getVolume() const
{
    return ui->volumeSlider->value();
}

void ConfigDialog::setKeyBindings(const KeyBindings &bindings)
{
    ui->upKeyButton->setKey(bindings.up);
    ui->downKeyButton->setKey(bindings.down);
    ui->leftKeyButton->setKey(bindings.left);
    ui->rightKeyButton->setKey(bindings.right);

    ui->button1KeyButton->setKey(bindings.button1);
    ui->button2KeyButton->setKey(bindings.button2);
    ui->button3KeyButton->setKey(bindings.button3);
    ui->button4KeyButton->setKey(bindings.button4);

    ui->bigLeftKeyButton->setKey(bindings.bigLeft);
    ui->littleLeftKeyButton->setKey(bindings.littleLeft);
    ui->littleRightKeyButton->setKey(bindings.littleRight);
    ui->bigRightKeyButton->setKey(bindings.bigRight);

    ui->menuKeyButton->setKey(bindings.menu);
    ui->pauseKeyButton->setKey(bindings.pause);
}

KeyBindings ConfigDialog::getKeyBindings() const
{
    KeyBindings bindings;
    bindings.up = ui->upKeyButton->key();
    bindings.down = ui->downKeyButton->key();
    bindings.left = ui->leftKeyButton->key();
    bindings.right = ui->rightKeyButton->key();

    bindings.button1 = ui->button1KeyButton->key();
    bindings.button2 = ui->button2KeyButton->key();
    bindings.button3 = ui->button3KeyButton->key();
    bindings.button4 = ui->button4KeyButton->key();

    bindings.bigLeft = ui->bigLeftKeyButton->key();
    bindings.littleLeft = ui->littleLeftKeyButton->key();
    bindings.littleRight = ui->littleRightKeyButton->key();
    bindings.bigRight = ui->bigRightKeyButton->key();

    bindings.menu = ui->menuKeyButton->key();
    bindings.pause = ui->pauseKeyButton->key();
    return bindings;
}

void ConfigDialog::onResetKeys()
{
    setKeyBindings(KeyBindings());
}

void ConfigDialog::setBiosPath(const QString &path)
{
    ui->biosPathEdit->setText(path);
}

QString ConfigDialog::getBiosPath() const
{
    return ui->biosPathEdit->text();
}

void ConfigDialog::onBrowseBios()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Select Boot ROM (BIOS)"),
        QString(),
        tr("Boot ROM Binary (*.bin);;All Files (*)")
    );

    if (!filename.isEmpty()) {
        ui->biosPathEdit->setText(filename);
    }
}

void ConfigDialog::onClearBios()
{
    ui->biosPathEdit->clear();
}
