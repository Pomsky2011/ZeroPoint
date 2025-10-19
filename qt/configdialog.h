#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class ConfigDialog; }
QT_END_NAMESPACE

class ConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigDialog(QWidget *parent = nullptr);
    ~ConfigDialog();

    // Display settings
    void setWindowScale(int scale);
    int getWindowScale() const;
    void setVSync(bool enabled);
    bool getVSync() const;

    // Audio settings
    void setAudioEnabled(bool enabled);
    bool getAudioEnabled() const;
    void setVolume(int volume);
    int getVolume() const;

private:
    Ui::ConfigDialog *ui;
};

#endif // CONFIGDIALOG_H
