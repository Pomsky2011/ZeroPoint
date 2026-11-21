#include "emulatorwidget.h"
#include "serial_host.h"
#include <QFile>
#include <QFileInfo>
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

    frameBuffer.fill(Qt::black);   // no System yet: the widget stays black until a cartridge is loaded
}

EmulatorWidget::~EmulatorWidget()
{
    joinEmuThread();   // `system` (declared first, so destroyed last) outlives nothing that touches it
    if (timerId != -1) {
        killTimer(timerId);
    }
}

QSize EmulatorWidget::sizeHint() const
{
    return QSize(ZeroPoint::FB_WIDTH * 2, ZeroPoint::FULL_HEIGHT * 2);
}

void EmulatorWidget::createSystem()
{
    destroySystem();
    system = std::make_unique<ZeroPoint::System>();
    system->setVerifySignatures(ZeroPoint::System::verifySignaturesDefault());   // ZEROPOINT_NO_VERIFY=1 to run unsigned ROMs
    system->setDevUnit(devUnit);   // before any boot: the Boot ROM reads it (a movie being played overrides it)
}

void EmulatorWidget::destroySystem()
{
    halt();
    session = ZeroPoint::MovieSession();   // its System* would dangle
    system.reset();
    poweredOn = false;
}

bool EmulatorWidget::applyBootROM()
{
    // Empty path: leave System's native (HLE) boot alone - loadBootROM() has
    // replace semantics, so there's nothing to do.
    if (bootRomPath.isEmpty()) {
        system->useHLEBootROM();   // a movie may have left a real one mapped
        return true;
    }
    return system->loadBootROM(bootRomPath.toStdString());
}

bool EmulatorWidget::applyApuBios()
{
    // Empty path: no APU internal BIOS - unlike the CPU side, there's no
    // built-in default to fall back to, so this just leaves APU memory
    // zero-filled (an idle NOP loop at $8000) if nothing was ever loaded.
    if (apuBiosPath.isEmpty()) {
        system->useHLEApuBios();
        return true;
    }
    return system->loadAPUBios(apuBiosPath.toStdString());
}

bool EmulatorWidget::loadROM(const QString &path)
{
    createSystem();   // old machine (and its emulation thread) goes first, THEN the new one exists
    movieKind = 0;   // loading a cartridge abandons any movie (finishMovie() is the way to keep a recording)
    currentRomPath = path;
    if (!system->loadROM(path.toStdString()) || !applyBootROM() || !applyApuBios()) {
        destroySystem();
        return false;
    }
    wireSerial();
    system->powerOn();
    poweredOn = true;
    updateFrameBuffer();
    update();
    return true;
}

bool EmulatorWidget::bootBIOS()
{
    createSystem();
    movieKind = 0;
    currentRomPath.clear();
    if (!applyBootROM() || !applyApuBios()) {
        destroySystem();
        return false;
    }
    wireSerial();
    system->powerOn();
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
    destroySystem();
}

void EmulatorWidget::halt()
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
        drainSerialOutput();   // whatever the guest sent in its last ticks
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
    if (!poweredOn) {
        return;
    }
    system->softReset();
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
        drainSerialOutput();
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
    system->setVideoThread(ZeroPoint::System::videoThreadDefault());   // PPU/display catch-up beside the CPU
    system->setIdlePollSkip(ZeroPoint::System::idlePollSkipDefault());
    auto fpsWindowStart = Clock::now();
    auto lastPublish = fpsWindowStart;
    int fpsSystemFrames = 0;   // System frames stepped in the current FPS window

    while (!stopRequested.load(std::memory_order_relaxed)) {
        unsigned actions = pendingActions.exchange(0);
        if (resetRequested.exchange(false)) actions |= ACT_SOFT_RESET;
        const int kind = movieKind.load(std::memory_order_relaxed);
        if (kind != 2) {   // (during playback the movie is in control: keys and buttons do nothing)
            const uint32_t input = packedInput.load(std::memory_order_relaxed);
            const uint32_t input2 = packedInput2.load(std::memory_order_relaxed);
            const bool two = twoPlayers.load(std::memory_order_relaxed);
            const uint8_t d1 = input & 0xFF, c1 = (input >> 8) & 0xFF, b1 = (input >> 16) & 0xFF;
            const uint8_t d2 = two ? (input2 & 0xFF) : 0, b2 = two ? ((input2 >> 16) & 0xFF) : 0;
            const uint8_t c2 = two ? static_cast<uint8_t>(((input2 >> 8) & 0xFF) | PlayerInput::CTRL_CONNECTION) : 0;
            std::vector<uint8_t> swapped;
            std::string swappedTitle;
            if (actions & ACT_SWAP) {
                std::lock_guard<std::mutex> lock(swapMutex);
                swapped.swap(swapImage);
                swappedTitle = swapTitle;
            }
            if (kind == 1) {   // recording: logged at the exact movie cycle, then applied
                session.input(0, d1, c1, b1);
                session.input(1, d2, c2, b2);
                if (actions & ACT_SOFT_RESET) session.softReset();
                if (actions & ACT_HARD_RESET) session.hardReset();
                if (actions & ACT_YANK) session.yank();
                if (!swapped.empty()) session.insert(swapped, swappedTitle);
            } else {
                system->getCPU().setPlayerInput(d1, c1, b1);
                system->getCPU().setPlayer2Input(d2, c2, b2);
                if (actions & ACT_SOFT_RESET) system->softReset();
                if (actions & ACT_HARD_RESET) system->hardReset();
                if (actions & ACT_YANK) system->ejectCartridge();
                if (!swapped.empty()) system->insertCartridge(swapped);
            }
            if (actions & (ACT_SOFT_RESET | ACT_HARD_RESET)) audio.flush();
        }

        serialTick(kind);

        const auto now = Clock::now();
        int due = pacer.framesDue(std::chrono::duration<double>(now.time_since_epoch()).count());
        if (due > MAX_BATCH) due = MAX_BATCH;
        for (int i = 0; i < due; i++) {
            system->getDisplay().setLatchEnabled(i + 1 == due);   // only the last raster is published below
            if (kind != 0) {
                if (session.run(ZeroPoint::System::CYCLES_PER_FRAME) == 0 && kind == 2) break;   // playback is over
            } else {
                system->stepFrame();
            }
        }
        if (kind == 2 && !playbackReported &&
            (session.finished() || session.desynced() || !session.failure().empty())) {
            playbackReported = true;
            if (!session.failure().empty()) emit movieEnded(QString::fromStdString(session.failure()), false);
            else if (session.desynced()) emit movieEnded(QString::fromStdString(session.desyncReport()), false);
            else emit movieEnded(tr("Movie finished: %1 checkpoints verified").arg(session.checkpointsVerified()), true);
        }
        pacer.stepped(due);
        fpsSystemFrames += due;

        if (due > 0) {
            const auto &samples = system->getAudioBuffer();
            audio.queue(samples.data(), static_cast<int>(samples.size() / 2));
            system->clearAudioBuffer();
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

    if (setPlayer2Key(key, true)) {
        updatePlayer2Input();
        event->accept();
        return;
    }
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

    if (setPlayer2Key(key, false)) {
        updatePlayer2Input();
        event->accept();
        return;
    }
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

bool EmulatorWidget::setPlayer2Key(int key, bool down)
{
    switch (key) {
        case Qt::Key_I: p2Up = down; return true;
        case Qt::Key_K: p2Down = down; return true;
        case Qt::Key_J: p2Left = down; return true;
        case Qt::Key_L: p2Right = down; return true;
        case Qt::Key_7: p2Btn1 = down; return true;
        case Qt::Key_8: p2Btn2 = down; return true;
        case Qt::Key_9: p2Btn3 = down; return true;
        case Qt::Key_0: p2Btn4 = down; return true;
        case Qt::Key_U: p2BigLeft = down; return true;
        case Qt::Key_O: p2BigRight = down; return true;
        case Qt::Key_N: p2LittleLeft = down; return true;
        case Qt::Key_M: p2LittleRight = down; return true;
        case Qt::Key_Backspace: p2Menu = down; return true;
        case Qt::Key_P: p2Pause = down; return true;
        default: return false;
    }
}

void EmulatorWidget::updatePlayer2Input()
{
    uint8_t direction = 0;
    uint8_t control = 0;   // CTRL_CONNECTION is added by the emulation thread when two players are on
    if (p2Up && p2Down && p2Left && p2Right) control |= PlayerInput::CTRL_CENTER;
    else if (p2Up && p2Left) direction = PlayerInput::DIR_UPLEFT;
    else if (p2Up && p2Right) direction = PlayerInput::DIR_UPRIGHT;
    else if (p2Down && p2Left) direction = PlayerInput::DIR_DOWNLEFT;
    else if (p2Down && p2Right) direction = PlayerInput::DIR_DOWNRIGHT;
    else if (p2Up) direction = PlayerInput::DIR_UP;
    else if (p2Down) direction = PlayerInput::DIR_DOWN;
    else if (p2Left) direction = PlayerInput::DIR_LEFT;
    else if (p2Right) direction = PlayerInput::DIR_RIGHT;
    if (p2BigLeft) control |= PlayerInput::CTRL_BIGLEFT;
    if (p2BigRight) control |= PlayerInput::CTRL_BIGRIGHT;
    if (p2LittleLeft) control |= PlayerInput::CTRL_LITTLELEFT;
    if (p2LittleRight) control |= PlayerInput::CTRL_LITTLERIGHT;
    if (p2Menu) control |= PlayerInput::CTRL_MENU;
    if (p2Pause) control |= PlayerInput::CTRL_PAUSE;
    uint8_t buttons = 0;
    if (p2Btn1) buttons |= PlayerInput::BTN_1;
    if (p2Btn2) buttons |= PlayerInput::BTN_2;
    if (p2Btn3) buttons |= PlayerInput::BTN_3;
    if (p2Btn4) buttons |= PlayerInput::BTN_4;
    packedInput2.store(static_cast<uint32_t>(direction) | (static_cast<uint32_t>(control) << 8) |
                           (static_cast<uint32_t>(buttons) << 16),
                       std::memory_order_relaxed);
}

void EmulatorWidget::hardReset()
{
    if (running) {
        pendingActions |= ACT_HARD_RESET;
        return;
    }
    if (poweredOn) {
        system->hardReset();
        audio.flush();
        updateFrameBuffer();
        update();
    }
}

void EmulatorWidget::yankCartridge()
{
    if (running) {
        pendingActions |= ACT_YANK;
        return;
    }
    if (poweredOn) system->ejectCartridge();
}

bool EmulatorWidget::swapCartridge(const QString &romPath, QString *error)
{
    QFile file(romPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("Can't read %1").arg(romPath);
        return false;
    }
    const QByteArray bytes = file.readAll();
    std::vector<uint8_t> image(bytes.begin(), bytes.end());
    ZeroPoint::ROM probe;
    if (!probe.loadMemory(image.data(), image.size())) {
        if (error) *error = QString::fromStdString(probe.getError());
        return false;
    }
    if (running) {
        {
            std::lock_guard<std::mutex> lock(swapMutex);
            swapImage = std::move(image);
            swapTitle = QFileInfo(romPath).fileName().toStdString();
        }
        pendingActions |= ACT_SWAP;
        return true;
    }
    if (poweredOn) system->insertCartridge(image);
    return true;
}

bool EmulatorWidget::startRecording(const QString &moviePath, const QString &author, QString *error)
{
    createSystem();   // a movie starts from a freshly constructed System
    ZeroPoint::MovieSession::RecordSetup setup;
    setup.bootRomPath = bootRomPath.toStdString();
    setup.apuBiosPath = apuBiosPath.toStdString();
    setup.cartridgePath = currentRomPath.toStdString();
    setup.verifySignatures = ZeroPoint::System::verifySignaturesDefault();
    setup.devUnit = devUnit;
    setup.author = author.toStdString();
    std::string err;
    if (!session.beginRecording(*system, setup, err)) {
        if (error) *error = QString::fromStdString(err);
        destroySystem();
        return false;
    }
    wireSerial();
    recordPath = moviePath.toStdString();
    movieKind = 1;
    poweredOn = true;
    audio.flush();
    updateFrameBuffer();
    update();
    return true;
}

bool EmulatorWidget::startPlayback(const QString &moviePath, const QStringList &romPaths, QString *error)
{
    halt();   // `playingMovie` is the emulation thread's too while it runs
    std::string err;
    if (!ZeroPoint::Movie::load(moviePath.toStdString(), playingMovie, err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    createSystem();
    library = ZeroPoint::RomLibrary();
    library.add(QFileInfo(moviePath).absolutePath().toStdString());
    for (const QString &p : romPaths) library.add(p.toStdString());
    if (!session.beginPlayback(*system, playingMovie, library, err)) {
        if (error) *error = QString::fromStdString(err);
        destroySystem();
        return false;
    }
    wireSerial();   // (the movie decided whether this is a dev unit)
    movieKind = 2;
    playbackReported = false;
    poweredOn = true;
    audio.flush();
    updateFrameBuffer();
    update();
    return true;
}

QString EmulatorWidget::finishMovie()
{
    halt();   // joins the emulation thread: `session` and `system` are ours again
    const int kind = movieKind.exchange(0);
    QString summary;
    if (kind == 1) {
        const ZeroPoint::Movie recorded = session.finishRecording();   // takes a last checkpoint: needs the System
        std::string err;
        if (!recorded.save(recordPath, err)) summary = QString::fromStdString(err);
        else summary = tr("Saved %1: %2 events over %3 s").arg(QString::fromStdString(recordPath)).arg(recorded.events.size())
            .arg(static_cast<double>(recorded.lengthCycles) / 68011355.0, 0, 'f', 2);
    } else if (kind == 2) {
        if (!session.failure().empty()) summary = QString::fromStdString(session.failure());
        else if (session.desynced()) summary = QString::fromStdString(session.desyncReport());
        else summary = session.finished() ? tr("Movie finished: %1 checkpoints verified").arg(session.checkpointsVerified())
                                          : tr("Playback stopped");
    }
    destroySystem();   // stopping tears the machine down, movie or not
    return summary;
}

void EmulatorWidget::wireSerial()
{
    if (!system->isDevUnit()) return;
    if (!serialHost && (!serialOuts.isEmpty() || !serialIns.isEmpty() || serialTcpPort >= 0)) {
        auto host = std::make_unique<ZeroPoint::SerialHost>();
        std::string err;
        bool ok = true;
        for (const QString &o : serialOuts) ok = host->addOutput(o.toStdString(), err) && ok;
        for (const QString &i : serialIns) ok = host->addInput(i.toStdString(), err) && ok;
        if (serialTcpPort >= 0) {
            if (serialTcpPort > 65535) { err = "the serial TCP port must be 0-65535"; ok = false; }
            else ok = host->listenTcp(static_cast<uint16_t>(serialTcpPort), err) && ok;
        }
        if (!ok) emit serialSetupFailed(QString::fromStdString(err));
        serialHost = std::move(host);   // what did open still works
    }
    system->setSerialSink([this](uint8_t b) {
        std::lock_guard<std::mutex> lock(serialMutex);
        if (serialTx.size() < (1 << 20)) serialTx.append(static_cast<char>(b));   // (nobody draining: don't grow forever)
        if (serialHost) serialHost->send(b);
    });
}

void EmulatorWidget::serialSend(const QByteArray &bytes)
{
    std::lock_guard<std::mutex> lock(serialMutex);
    for (char c : bytes) {
        if (serialGuiRx.size() < (1 << 16)) serialGuiRx.push_back(static_cast<uint8_t>(c));
    }
}

// A few bytes per ~60 Hz tick while the guest's queue has room, like the SDL frontend: through the movie session when
// recording (it logs them at their exact cycle), straight in otherwise. A replay is in control and takes none, but the
// typed bytes are still consumed so they don't pile up.
void EmulatorWidget::serialTick(int movieKind)
{
    if (!system->isDevUnit()) return;
    for (int n = 0; n < 192 && system->serialRxHasRoom(); n++) {
        uint8_t b;
        bool got = false;
        {
            std::lock_guard<std::mutex> lock(serialMutex);
            if (!serialGuiRx.empty()) {
                b = serialGuiRx.front();
                serialGuiRx.pop_front();
                got = true;
            }
        }
        if (!got && !(serialHost && serialHost->receive(b))) break;
        if (movieKind == 1) session.serialIn(b);
        else if (movieKind != 2) system->serialReceive(b);
    }
    if (serialHost) serialHost->flush();
}

void EmulatorWidget::drainSerialOutput()
{
    QByteArray out;
    {
        std::lock_guard<std::mutex> lock(serialMutex);
        out.swap(serialTx);
    }
    if (!out.isEmpty()) emit serialOutput(out);
}

void EmulatorWidget::updateFrameBuffer()
{
    if (!system) {
        return;
    }
    convertFrame(frameBuffer);
}

void EmulatorWidget::convertFrame(QImage &dst) const
{
    // getPixel() is a live read from the small rolling buffer - black outside
    // whatever few scanlines are currently in its window. getScanline() reads
    // the latched, complete output frame instead (same one the SDL frontend's
    // getScanlineSDL() uses), which is what a full-frame blit needs.
    const ZeroPoint::Display &display = system->getDisplay();
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
