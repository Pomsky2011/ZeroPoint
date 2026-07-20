#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QDialog>
#include <QString>
#include "keybindings.h"

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
    void setSmoothFiltering(bool smooth);
    bool getSmoothFiltering() const;

    // Audio settings
    void setAudioEnabled(bool enabled);
    bool getAudioEnabled() const;
    void setVolume(int volume);
    int getVolume() const;

    // Input settings
    void setKeyBindings(const KeyBindings &bindings);
    KeyBindings getKeyBindings() const;

    // BIOS / Boot ROM settings. Empty path means "use the built-in stub".
    void setBiosPath(const QString &path);
    QString getBiosPath() const;

private slots:
    void onResetKeys();
    void onBrowseBios();
    void onClearBios();

private:
    Ui::ConfigDialog *ui;
};

#endif // CONFIGDIALOG_H
