#include "configdialog.h"
#include "ui_configdialog.h"

ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigDialog)
{
    ui->setupUi(this);
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
