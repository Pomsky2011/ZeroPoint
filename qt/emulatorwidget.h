#ifndef EMULATORWIDGET_H
#define EMULATORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QString>
#include "system.h"
#include "keybindings.h"

class EmulatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EmulatorWidget(QWidget *parent = nullptr);
    ~EmulatorWidget();

    bool loadROM(const QString &path);

    // Power on with just the Boot ROM mapped and no cartridge - e.g. to watch
    // a splash screen or exercise signature-verification failure handling.
    // Real hardware behaves the same way with an empty cartridge slot.
    bool bootBIOS();

    void start();
    void stop();
    void reset();
    bool isActive() const { return poweredOn; }

    // Empty path means "use the built-in default stub" (loaded at System
    // construction time). Takes effect on the next loadROM()/bootBIOS() call.
    void setBootROMPath(const QString &path) { bootRomPath = path; }

    void setKeyBindings(const KeyBindings &bindings) { keys = bindings; }
    void setSmoothFiltering(bool smooth) { smoothFiltering = smooth; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    ZeroPoint::System system;
    QImage frameBuffer;
    int timerId;
    bool running;
    bool poweredOn;
    QString bootRomPath;
    bool smoothFiltering;

    KeyBindings keys;

    // Held-key state for Player 1 input. See cpu.h PlayerInput:: for the
    // register bit layout these are packed into each frame.
    bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
    bool keyBtn1 = false, keyBtn2 = false, keyBtn3 = false, keyBtn4 = false;
    bool keyBigLeft = false, keyLittleLeft = false, keyLittleRight = false, keyBigRight = false;
    bool keyMenu = false, keyPause = false;

    bool applyBootROM();
    void updateFrameBuffer();
    void updatePlayerInput();
    void fillTestPattern();
};

#endif // EMULATORWIDGET_H
