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
    , poweredOn(false)
    , smoothFiltering(false)
{
    // Free to resize/scale - paintEvent letterboxes to the display's 1:1
    // aspect ratio inside whatever size the window (or fullscreen) gives us,
    // rather than pinning the widget to one fixed pixel size.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT);
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

QSize EmulatorWidget::sizeHint() const
{
    return QSize(ZeroPoint::FB_WIDTH * 2, ZeroPoint::FULL_HEIGHT * 2);
}

bool EmulatorWidget::applyBootROM()
{
    // Empty path: leave the default stub System() already installed alone -
    // loadBootROM() has replace semantics, so there's nothing to do.
    if (bootRomPath.isEmpty()) {
        return true;
    }
    return system.loadBootROM(bootRomPath.toStdString());
}

bool EmulatorWidget::applyApuBios()
{
    // Empty path: no APU internal BIOS - unlike the CPU side, there's no
    // built-in default stub to fall back to, so this just leaves APU memory
    // zero-filled (an idle NOP loop at $8000) if nothing was ever loaded.
    if (apuBiosPath.isEmpty()) {
        return true;
    }
    return system.loadAPUBios(apuBiosPath.toStdString());
}

bool EmulatorWidget::loadROM(const QString &path)
{
    stop();
    if (!system.loadROM(path.toStdString())) {
        return false;
    }
    if (!applyBootROM()) {
        return false;
    }
    if (!applyApuBios()) {
        return false;
    }
    system.powerOn();
    poweredOn = true;
    updateFrameBuffer();
    update();
    return true;
}

bool EmulatorWidget::bootBIOS()
{
    stop();
    if (!applyBootROM()) {
        return false;
    }
    if (!applyApuBios()) {
        return false;
    }
    system.powerOn();
    poweredOn = true;
    updateFrameBuffer();
    update();
    return true;
}

void EmulatorWidget::start()
{
    if (!running && poweredOn) {
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
    if (poweredOn) {
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
    painter.fillRect(rect(), Qt::black);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothFiltering);

    // Letterbox/pillarbox to the largest centered square that fits, since
    // the display is a fixed 1:1 aspect ratio but the widget (window,
    // fullscreen, whatever) can be any size.
    int side = qMin(width(), height());
    QRect dest((width() - side) / 2, (height() - side) / 2, side, side);
    painter.drawImage(dest, frameBuffer);
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
    const int key = event->key();
    bool handled = true;

    if (key == keys.up) keyUp = true;
    else if (key == keys.down) keyDown = true;
    else if (key == keys.left) keyLeft = true;
    else if (key == keys.right) keyRight = true;
    else if (key == keys.button1) keyBtn1 = true;
    else if (key == keys.button2) keyBtn2 = true;
    else if (key == keys.button3) keyBtn3 = true;
    else if (key == keys.button4) keyBtn4 = true;
    else if (key == keys.bigLeft) keyBigLeft = true;
    else if (key == keys.littleLeft) keyLittleLeft = true;
    else if (key == keys.littleRight) keyLittleRight = true;
    else if (key == keys.bigRight) keyBigRight = true;
    else if (key == keys.menu) keyMenu = true;
    else if (key == keys.pause) keyPause = true;
    else handled = false;

    if (handled) {
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void EmulatorWidget::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();
    bool handled = true;

    if (key == keys.up) keyUp = false;
    else if (key == keys.down) keyDown = false;
    else if (key == keys.left) keyLeft = false;
    else if (key == keys.right) keyRight = false;
    else if (key == keys.button1) keyBtn1 = false;
    else if (key == keys.button2) keyBtn2 = false;
    else if (key == keys.button3) keyBtn3 = false;
    else if (key == keys.button4) keyBtn4 = false;
    else if (key == keys.bigLeft) keyBigLeft = false;
    else if (key == keys.littleLeft) keyLittleLeft = false;
    else if (key == keys.littleRight) keyLittleRight = false;
    else if (key == keys.bigRight) keyBigRight = false;
    else if (key == keys.menu) keyMenu = false;
    else if (key == keys.pause) keyPause = false;
    else handled = false;

    if (handled) {
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);
    }
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
    if (keyBtn1) buttons |= PlayerInput::BTN_1;
    if (keyBtn2) buttons |= PlayerInput::BTN_2;
    if (keyBtn3) buttons |= PlayerInput::BTN_3;
    if (keyBtn4) buttons |= PlayerInput::BTN_4;

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
    // getPixel() is a live read from the small rolling buffer - black outside
    // whatever few scanlines are currently in its window. getScanline() reads
    // the latched, complete output frame instead (same one the SDL frontend's
    // getScanlineSDL() uses), which is what a full-frame blit needs.
    const ZeroPoint::Display &display = system.getDisplay();
    ZeroPoint::Color32 row[ZeroPoint::FB_WIDTH];
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        display.getScanline(y, row);
        QRgb *scanline = reinterpret_cast<QRgb*>(frameBuffer.scanLine(y));
        for (int x = 0; x < ZeroPoint::FB_WIDTH; x++) {
            ZeroPoint::Color32 color = row[x];
            // RGBA32 format: RRGGBBAA
            uint8_t r = (color >> 24) & 0xFF;
            uint8_t g = (color >> 16) & 0xFF;
            uint8_t b = (color >> 8) & 0xFF;
            // Ignore alpha for now
            scanline[x] = qRgb(r, g, b);
        }
    }
}
