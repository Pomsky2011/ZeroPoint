#ifndef EMULATORWIDGET_H
#define EMULATORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include "system.h"
#include "movie.h"
#include "frame_pacer.h"
#include "keybindings.h"
#include "audiooutput.h"

namespace ZeroPoint { class SerialHost; }

class EmulatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EmulatorWidget(QWidget *parent = nullptr);
    ~EmulatorWidget();

    // Tears down any live System, builds a fresh one and powers it on with this cartridge.
    bool loadROM(const QString &path);

    // Power on with just the Boot ROM mapped and no cartridge - e.g. to watch
    // a splash screen or exercise signature-verification failure handling.
    // Real hardware behaves the same way with an empty cartridge slot.
    bool bootBIOS();

    void start();
    // Stops emulation AND tears the System down: there is no machine afterwards (isActive() is false) until the next
    // loadROM()/bootBIOS()/startRecording()/startPlayback() builds a new one. The last frame stays on screen.
    void stop();
    void reset();
    bool isActive() const { return poweredOn; }

    // ---- Movies (docs/movies.md) ------------------------------------------------------------------------------
    // Recording and playback both restart the machine from a power cycle: the movie pins the boot ROM, APU BIOS and
    // cartridge, and starts from the instant after power-on. Load the cartridge you want with loadROM() first
    // (recording starts with whatever it loaded), call these, then start().
    bool startRecording(const QString &moviePath, const QString &author, QString *error);
    bool startPlayback(const QString &moviePath, const QStringList &romPaths, QString *error);
    // Stops emulation; saves a recording or reports how a playback went. One line for the status bar.
    QString finishMovie();
    bool isRecording() const { return movieKind.load() == 1; }
    bool isPlayingMovie() const { return movieKind.load() == 2; }

    // The host's buttons on the machine, applied by the emulation thread between frames (and logged when recording;
    // ignored during playback, when the movie is in control). reset() above is the soft reset.
    void hardReset();                       // a power cycle
    void yankCartridge();
    bool swapCartridge(const QString &romPath, QString *error);   // Stop 'n' Swop
    void setTwoPlayers(bool on) { twoPlayers = on; }
    bool twoPlayersOn() const { return twoPlayers.load(); }

    // Empty path means "use the built-in default stub" (loaded at System
    // construction time). Takes effect on the next loadROM()/bootBIOS() call.
    void setBootROMPath(const QString &path) { bootRomPath = path; }

    // Empty path means "no APU internal BIOS" (APU memory stays zero-filled
    // at $8000 - there's no built-in default stub on the APU side the way
    // there is for the CPU). Takes effect on the next loadROM()/bootBIOS() call.
    void setApuBiosPath(const QString &path) { apuBiosPath = path; }

    void setKeyBindings(const KeyBindings &bindings) { keys = bindings; }
    void setSmoothFiltering(bool smooth) { smoothFiltering = smooth; }
    void setAudioEnabled(bool on) { audio.setEnabled(on); }
    void setAudioVolume(int percent) { audio.setVolume(percent); }

    // ---- Dev unit and the debug serial port (docs/debug-protocol.md) --------------------------------------------
    // A dev unit makes $D8003C-$D8003E a 115200-baud UART and makes signature checks always pass. Takes effect on the
    // next loadROM()/bootBIOS()/startRecording() (a movie being played decides for itself). Off by default: it is a
    // host setting, so it is never remembered between runs.
    void setDevUnit(bool on) { devUnit = on; }
    bool devUnitOn() const { return devUnit; }
    // Where the guest's bytes go besides the Serial Console, and where host bytes come from (the same endpoints as the
    // SDL frontend's --serial-out / --serial-in / --serial-tcp: "-" is stdout/stdin, otherwise a file or FIFO; port < 0
    // = no TCP). Opened the first time a dev unit powers on.
    void setSerialEndpoints(const QStringList &outs, const QStringList &ins, int tcpPort)
    { serialOuts = outs; serialIns = ins; serialTcpPort = tcpPort; }
    // Host -> guest: queued, then fed in by the emulation thread a few bytes per tick (recorded when recording).
    void serialSend(const QByteArray &bytes);

    QSize sizeHint() const override;

signals:
    // Guest -> host: what the guest transmitted since the last tick (emitted on the GUI thread).
    void serialOutput(const QByteArray &bytes);
    // An endpoint (file, FIFO, TCP port) couldn't be opened; the machine runs without it.
    void serialSetupFailed(const QString &message);

    // A playback reached its end, desynced, or failed (emitted from the emulation thread; queued to the GUI).
    void movieEnded(const QString &summary, bool ok);

    // Emulation speed as frames per second out of a full-speed 60: console
    // seconds actually stepped / wall seconds * 60, so 60 = full speed, 30 = half.
    // Emitted about once a second while running, and with 0 when emulation stops.
    void fpsChanged(int fps);

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    // Threading: while running, `system`, `pacer` and `audio.queue()` belong to emuThread alone (emuLoop). The
    // GUI thread only paints `frameBuffer`, swaps in the newest frame emuThread published, and talks to the
    // emulator through the atomics below. Everything else touches `system` only while the thread is joined.
    // Lifetime: null until a cartridge (or the BIOS alone) is loaded, destroyed by stop() / the next load. `poweredOn`
    // is true exactly while it exists.
    std::unique_ptr<ZeroPoint::System> system;
    QImage frameBuffer;         // GUI thread: what paintEvent draws
    QImage publishedFrame;      // newest finished frame; guarded by frameMutex
    QImage workFrame;           // emuThread's scratch frame, swapped into publishedFrame
    std::mutex frameMutex;
    bool frameReady = false;    // publishedFrame is newer than frameBuffer; guarded by frameMutex
    int timerId;
    ZeroPoint::FramePacer pacer{ZeroPoint::FramePacer::AUDIO_LEAD};
    AudioOutput audio;
    std::thread emuThread;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<uint32_t> packedInput{0};   // direction | control << 8 | buttons << 16, see updatePlayerInput()
    std::atomic<uint32_t> packedInput2{0};  // same for player 2 (without CTRL_CONNECTION: twoPlayers adds it)
    std::atomic<bool> twoPlayers{false};
    enum Action : unsigned { ACT_SOFT_RESET = 1, ACT_HARD_RESET = 2, ACT_YANK = 4, ACT_SWAP = 8 };
    std::atomic<unsigned> pendingActions{0};
    std::mutex swapMutex;                   // guards swapImage/swapTitle
    std::vector<uint8_t> swapImage;
    std::string swapTitle;

    // Movies. `session`/`library` belong to emuThread while it runs (like `system`).
    ZeroPoint::MovieSession session;
    ZeroPoint::RomLibrary library;
    ZeroPoint::Movie playingMovie;
    std::atomic<int> movieKind{0};          // 0 none, 1 recording, 2 playing
    std::string recordPath;
    QString currentRomPath;                 // what loadROM() loaded last (a recording starts with it in the slot)
    bool playbackReported = false;          // emuThread: movieEnded already emitted
    std::chrono::steady_clock::time_point nextTick;   // absolute deadline of the next 60 Hz repaint tick
    bool running;
    bool poweredOn;
    QString bootRomPath;
    QString apuBiosPath;
    bool smoothFiltering;

    KeyBindings keys;

    // Held-key state for Player 1 input. See cpu.h PlayerInput:: for the
    // register bit layout these are packed into each frame.
    bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
    bool keyBtn1 = false, keyBtn2 = false, keyBtn3 = false, keyBtn4 = false;
    bool keyBigLeft = false, keyLittleLeft = false, keyLittleRight = false, keyBigRight = false;
    bool keyMenu = false, keyPause = false;

    // Player 2's fixed keys (i/j/k/l, 7/8/9/0, u/o, n/m, backspace, p - the same as the SDL frontend).
    bool p2Up = false, p2Down = false, p2Left = false, p2Right = false;
    bool p2Btn1 = false, p2Btn2 = false, p2Btn3 = false, p2Btn4 = false;
    bool p2BigLeft = false, p2BigRight = false, p2LittleLeft = false, p2LittleRight = false;
    bool p2Menu = false, p2Pause = false;
    bool setPlayer2Key(int key, bool down);
    void updatePlayer2Input();

    void createSystem();                 // tear down any current System, construct a fresh one
    void destroySystem();                // halt() + free it; the session's pointer into it dies with it
    void halt();                         // stop the emulation thread and timer, keep the System
    bool applyBootROM();
    bool applyApuBios();
    void scheduleTick(std::chrono::steady_clock::time_point now);
    void emuLoop();                      // emuThread's body
    void joinEmuThread();
    void convertFrame(QImage &dst) const;   // latched display frame -> dst (system must be idle or the caller's)
    void updateFrameBuffer();
    void updatePlayerInput();

    // Dev unit / serial. The sink runs on whichever thread steps the System (emuThread, or the GUI thread while
    // stopped), so everything it and the GUI share is behind serialMutex.
    bool devUnit = false;
    QStringList serialOuts, serialIns;
    int serialTcpPort = -1;
    std::unique_ptr<ZeroPoint::SerialHost> serialHost;   // made on the first dev-unit power-on, kept for the widget's life
    std::mutex serialMutex;
    QByteArray serialTx;                    // guest -> GUI, drained by drainSerialOutput()
    std::deque<uint8_t> serialGuiRx;        // GUI -> guest (serialSend), drained by serialTick()
    void wireSerial();                      // after a System is built and its dev-unit setting is known
    void serialTick(int movieKind);         // emuThread: feed host bytes to the guest
    void drainSerialOutput();
};

#endif // EMULATORWIDGET_H
