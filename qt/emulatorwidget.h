#ifndef EMULATORWIDGET_H
#define EMULATORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QString>
#include "system.h"

class EmulatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EmulatorWidget(QWidget *parent = nullptr);
    ~EmulatorWidget();

    bool loadROM(const QString &path);
    void start();
    void stop();
    void reset();

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

    // Held-key state for Player 1 input. See cpu.h PlayerInput:: for the
    // register bit layout these are packed into each frame.
    bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
    bool keyZ = false, keyX = false, keyC = false, keyV = false;
    bool keyBigLeft = false, keyLittleLeft = false, keyLittleRight = false, keyBigRight = false;
    bool keyMenu = false, keyPause = false;

    void updateFrameBuffer();
    void updatePlayerInput();
    void fillTestPattern();
};

#endif // EMULATORWIDGET_H
