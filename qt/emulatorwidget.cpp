#include "emulatorwidget.h"
#include <QPainter>
#include <QTimerEvent>
#include <QKeyEvent>

using namespace ZeroPoint;

EmulatorWidget::EmulatorWidget(QWidget *parent)
    : QWidget(parent)
    , frameBuffer(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT, QImage::Format_RGB32)
    , timerId(-1)
    , running(false)
{
    setFixedSize(ZeroPoint::FB_WIDTH * 2, ZeroPoint::FULL_HEIGHT * 2);
    setFocusPolicy(Qt::StrongFocus);

    fillTestPattern();
    updateFrameBuffer();
}

EmulatorWidget::~EmulatorWidget()
{
    if (timerId != -1) {
        killTimer(timerId);
    }
}

bool EmulatorWidget::loadROM(const QString &path)
{
    stop();
    if (!system.loadROM(path.toStdString())) {
        return false;
    }
    system.reset();
    updateFrameBuffer();
    update();
    return true;
}

void EmulatorWidget::start()
{
    if (!running && system.isROMLoaded()) {
        running = true;
        setFocus();
        // Start timer for ~60 FPS
        timerId = startTimer(16); // ~60 Hz
    }
}

void EmulatorWidget::stop()
{
    if (running) {
        running = false;
        if (timerId != -1) {
            killTimer(timerId);
            timerId = -1;
        }
    }
}

void EmulatorWidget::reset()
{
    if (system.isROMLoaded()) {
        system.reset();
    } else {
        fillTestPattern();
    }
    updateFrameBuffer();
    update();
}

void EmulatorWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.drawImage(rect(), frameBuffer);
}

void EmulatorWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == timerId && running) {
        updatePlayerInput();
        system.stepFrame();
        updateFrameBuffer();
        update();
    }
}

void EmulatorWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Up:    keyUp = true; break;
        case Qt::Key_Down:  keyDown = true; break;
        case Qt::Key_Left:  keyLeft = true; break;
        case Qt::Key_Right: keyRight = true; break;
        case Qt::Key_Z:     keyZ = true; break;
        case Qt::Key_X:     keyX = true; break;
        case Qt::Key_C:     keyC = true; break;
        case Qt::Key_V:     keyV = true; break;
        case Qt::Key_A:     keyBigLeft = true; break;
        case Qt::Key_S:     keyLittleLeft = true; break;
        case Qt::Key_D:     keyLittleRight = true; break;
        case Qt::Key_F:     keyBigRight = true; break;
        case Qt::Key_Shift: keyMenu = true; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: keyPause = true; break;
        default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void EmulatorWidget::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Up:    keyUp = false; break;
        case Qt::Key_Down:  keyDown = false; break;
        case Qt::Key_Left:  keyLeft = false; break;
        case Qt::Key_Right: keyRight = false; break;
        case Qt::Key_Z:     keyZ = false; break;
        case Qt::Key_X:     keyX = false; break;
        case Qt::Key_C:     keyC = false; break;
        case Qt::Key_V:     keyV = false; break;
        case Qt::Key_A:     keyBigLeft = false; break;
        case Qt::Key_S:     keyLittleLeft = false; break;
        case Qt::Key_D:     keyLittleRight = false; break;
        case Qt::Key_F:     keyBigRight = false; break;
        case Qt::Key_Shift: keyMenu = false; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: keyPause = false; break;
        default: QWidget::keyReleaseEvent(event); return;
    }
    event->accept();
}

void EmulatorWidget::updatePlayerInput()
{
    uint8_t direction = 0;
    uint8_t control = PlayerInput::CTRL_CONNECTION;

    // Holding all four arrows at once has no directional meaning of its own,
    // so it doubles as the D-pad's center-click on a keyboard. Otherwise,
    // an adjacent pair gives a diagonal; a single key gives a cardinal.
    if (keyUp && keyDown && keyLeft && keyRight) {
        control |= PlayerInput::CTRL_CENTER;
    } else if (keyUp && keyLeft) {
        direction = PlayerInput::DIR_UPLEFT;
    } else if (keyUp && keyRight) {
        direction = PlayerInput::DIR_UPRIGHT;
    } else if (keyDown && keyLeft) {
        direction = PlayerInput::DIR_DOWNLEFT;
    } else if (keyDown && keyRight) {
        direction = PlayerInput::DIR_DOWNRIGHT;
    } else if (keyUp) {
        direction = PlayerInput::DIR_UP;
    } else if (keyDown) {
        direction = PlayerInput::DIR_DOWN;
    } else if (keyLeft) {
        direction = PlayerInput::DIR_LEFT;
    } else if (keyRight) {
        direction = PlayerInput::DIR_RIGHT;
    }

    if (keyBigLeft)     control |= PlayerInput::CTRL_BIGLEFT;
    if (keyBigRight)    control |= PlayerInput::CTRL_BIGRIGHT;
    if (keyLittleLeft)  control |= PlayerInput::CTRL_LITTLELEFT;
    if (keyLittleRight) control |= PlayerInput::CTRL_LITTLERIGHT;
    if (keyMenu)        control |= PlayerInput::CTRL_MENU;
    if (keyPause)       control |= PlayerInput::CTRL_PAUSE;

    uint8_t buttons = 0;
    if (keyZ) buttons |= PlayerInput::BTN_1;
    if (keyX) buttons |= PlayerInput::BTN_2;
    if (keyC) buttons |= PlayerInput::BTN_3;
    if (keyV) buttons |= PlayerInput::BTN_4;

    system.getCPU().setPlayerInput(direction, control, buttons);
}

void EmulatorWidget::fillTestPattern()
{
    ZeroPoint::Display &display = system.getDisplay();
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        for (int x = 0; x < ZeroPoint::FB_WIDTH; x++) {
            uint16_t red = (x * 31 / 255) << 1;
            uint16_t blue = (y * 31 / 255) << 11;
            display.setPixel(x, y, red | blue);
        }
    }
}

void EmulatorWidget::updateFrameBuffer()
{
    // Use getPixel() to read from rolling buffer
    // Pixels outside the 8-scanline window will be black
    const ZeroPoint::Display &display = system.getDisplay();
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        QRgb *scanline = reinterpret_cast<QRgb*>(frameBuffer.scanLine(y));
        for (int x = 0; x < ZeroPoint::FB_WIDTH; x++) {
            ZeroPoint::Color32 color = display.getPixel(x, y);
            // RGBA32 format: RRGGBBAA
            uint8_t r = (color >> 24) & 0xFF;
            uint8_t g = (color >> 16) & 0xFF;
            uint8_t b = (color >> 8) & 0xFF;
            // Ignore alpha for now
            scanline[x] = qRgb(r, g, b);
        }
    }
}
