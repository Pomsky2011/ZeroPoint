#ifndef EMULATORWIDGET_H
#define EMULATORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include "display.h"
#include "rom.h"

class EmulatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EmulatorWidget(QWidget *parent = nullptr);
    ~EmulatorWidget();

    void loadROM(ZeroPoint::ROM *rom);
    void start();
    void stop();
    void reset();

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    ZeroPoint::Display display;
    ZeroPoint::ROM *currentROM;
    QImage frameBuffer;
    int timerId;
    bool running;

    void updateFrameBuffer();
    QRgb convertColor(ZeroPoint::Color16 color);
};

#endif // EMULATORWIDGET_H
