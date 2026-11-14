#include "emulatorwidget.h"
#include <QPainter>
#include <QTimerEvent>
#include <algorithm>
#include <chrono>
#include <QKeyEvent>

using namespace ZeroPoint;

namespace {
constexpr double TICK_SECONDS = 1.0 / 60.0;
}

EmulatorWidget::EmulatorWidget(QWidget *parent)
    : QWidget(parent)
    , frameBuffer(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT, QImage::Format_RGB32)
    , publishedFrame(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT, QImage::Format_RGB32)
    , workFrame(ZeroPoint::FB_WIDTH, ZeroPoint::FULL_HEIGHT, QImage::Format_RGB32)
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
    joinEmuThread();
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
        // Stepping runs on its own thread; this timer only repaints at ~60 Hz.
        pacer.reset();
        stopRequested = false;
        updatePlayerInput();
        emuThread = std::thread(&EmulatorWidget::emuLoop, this);
        scheduleTick(std::chrono::steady_clock::now());
    }
}

void EmulatorWidget::stop()
{
    if (running) {
        running = false;
        joinEmuThread();
        {   // the thread's last frame is the one to leave on screen
            std::lock_guard<std::mutex> lock(frameMutex);
            if (frameReady) {
                frameBuffer.swap(publishedFrame);
                frameReady = false;
            }
        }
        update();
        if (timerId != -1) {
            killTimer(timerId);
            timerId = -1;
        }
        audio.flush();   // don't keep playing the tail of the queue after Stop
        emit fpsChanged(0);
    }
}

void EmulatorWidget::reset()
{
    if (running) {
        resetRequested = true;   // emuThread owns `system`; it resets between frames
        return;
    }
    if (poweredOn) {
        system.reset();
    } else {
        fillTestPattern();
    }
    audio.flush();
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
        const auto tickStart = std::chrono::steady_clock::now();
        updatePlayerInput();
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (frameReady) {
                frameBuffer.swap(publishedFrame);
                frameReady = false;
                fresh = true;
            }
        }
        if (fresh) {
            update();
        }
        scheduleTick(tickStart);
    }
}

void EmulatorWidget::joinEmuThread()
{
    if (emuThread.joinable()) {
        stopRequested = true;
        emuThread.join();
    }
}

// emuThread's body. A System frame is ~1.3 ms of console time, not 1/60 s: step as many as the console clock is
// behind the wall clock (frame_pacer.h), hand audio to the device, and publish a finished frame for the GUI
// about once per 1/60 s. Painting and the host's event loop never take time from stepping.
void EmulatorWidget::emuLoop()
{
    using Clock = std::chrono::steady_clock;
    constexpr int MAX_BATCH = 16;   // ~21 ms of console time: keeps audio and frames flowing while catching up
    const auto publishEvery = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(TICK_SECONDS));
    system.setVideoThread(ZeroPoint::System::videoThreadDefault());   // PPU/display catch-up beside the CPU
    auto fpsWindowStart = Clock::now();
    auto lastPublish = fpsWindowStart;
    int fpsSystemFrames = 0;   // System frames stepped in the current FPS window

    while (!stopRequested.load(std::memory_order_relaxed)) {
        if (resetRequested.exchange(false)) {
            system.reset();
            audio.flush();
        }
        const uint32_t input = packedInput.load(std::memory_order_relaxed);
        system.getCPU().setPlayerInput(input & 0xFF, (input >> 8) & 0xFF, (input >> 16) & 0xFF);

        const auto now = Clock::now();
        int due = pacer.framesDue(std::chrono::duration<double>(now.time_since_epoch()).count());
        if (due > MAX_BATCH) due = MAX_BATCH;
        for (int i = 0; i < due; i++) {
            system.getDisplay().setLatchEnabled(i + 1 == due);   // only the last raster is published below
            system.stepFrame();
        }
        pacer.stepped(due);
        fpsSystemFrames += due;

        if (due > 0) {
            const auto &samples = system.getAudioBuffer();
            audio.queue(samples.data(), static_cast<int>(samples.size() / 2));
            system.clearAudioBuffer();
        }

        const auto after = Clock::now();
        if (due > 0 && after - lastPublish >= publishEvery) {
            convertFrame(workFrame);
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                publishedFrame.swap(workFrame);
                frameReady = true;
            }
            lastPublish = after;
        }

        const double fpsElapsed = std::chrono::duration<double>(after - fpsWindowStart).count();
        if (fpsElapsed >= 1.0) {
            const double consoleSeconds = fpsSystemFrames * ZeroPoint::FramePacer::FRAME_SECONDS;
            emit fpsChanged(static_cast<int>(consoleSeconds / fpsElapsed / TICK_SECONDS + 0.5));
            fpsWindowStart = after;
            fpsSystemFrames = 0;
        }

        if (due == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));   // ahead of the wall clock
        }
    }
}

// Qt timers only take whole milliseconds, so a fixed 16 ms interval runs at 62.5 Hz. Instead each tick is
// scheduled against an absolute 1/60 s deadline (re-armed with the whole ms left until it), so the rounding
// averages out to exactly 60 Hz.
void EmulatorWidget::scheduleTick(std::chrono::steady_clock::time_point now)
{
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(TICK_SECONDS));
    if (timerId == -1) {
        nextTick = now;   // first tick of a run
    }
    nextTick += period;
    if (nextTick < now) {
        nextTick = now + period;   // fell behind (slow tick): don't burst to catch up, the pacer does that
    }
    const auto untilNext = std::chrono::duration_cast<std::chrono::milliseconds>(
        nextTick - std::chrono::steady_clock::now() + std::chrono::microseconds(500));   // round, not floor
    if (timerId != -1) {
        killTimer(timerId);
    }
    timerId = startTimer(std::max<std::chrono::milliseconds>(untilNext, std::chrono::milliseconds(1)),
                         Qt::PreciseTimer);
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
        updatePlayerInput();
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
        updatePlayerInput();
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

    // emuThread applies it to the CPU's port registers at the top of each batch of frames
    packedInput.store(static_cast<uint32_t>(direction) | (static_cast<uint32_t>(control) << 8) |
                          (static_cast<uint32_t>(buttons) << 16),
                      std::memory_order_relaxed);
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
    convertFrame(frameBuffer);
}

void EmulatorWidget::convertFrame(QImage &dst) const
{
    // getPixel() is a live read from the small rolling buffer - black outside
    // whatever few scanlines are currently in its window. getScanline() reads
    // the latched, complete output frame instead (same one the SDL frontend's
    // getScanlineSDL() uses), which is what a full-frame blit needs.
    const ZeroPoint::Display &display = system.getDisplay();
    ZeroPoint::Color32 row[ZeroPoint::FB_WIDTH];
    for (int y = 0; y < ZeroPoint::FULL_HEIGHT; y++) {
        display.getScanline(y, row);
        QRgb *scanline = reinterpret_cast<QRgb*>(dst.scanLine(y));
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
