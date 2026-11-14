#ifndef EMULATORWIDGET_H
#define EMULATORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QString>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "system.h"
#include "frame_pacer.h"
#include "keybindings.h"
#include "audiooutput.h"

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

    // Empty path means "no APU internal BIOS" (APU memory stays zero-filled
    // at $8000 - there's no built-in default stub on the APU side the way
    // there is for the CPU). Takes effect on the next loadROM()/bootBIOS() call.
    void setApuBiosPath(const QString &path) { apuBiosPath = path; }

    void setKeyBindings(const KeyBindings &bindings) { keys = bindings; }
    void setSmoothFiltering(bool smooth) { smoothFiltering = smooth; }
    void setAudioEnabled(bool on) { audio.setEnabled(on); }
    void setAudioVolume(int percent) { audio.setVolume(percent); }

    QSize sizeHint() const override;

signals:
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
    ZeroPoint::System system;
    QImage frameBuffer;         // GUI thread: what paintEvent draws
    QImage publishedFrame;      // newest finished frame; guarded by frameMutex
    QImage workFrame;           // emuThread's scratch frame, swapped into publishedFrame
    std::mutex frameMutex;
    bool frameReady = false;    // publishedFrame is newer than frameBuffer; guarded by frameMutex
    int timerId;
    ZeroPoint::FramePacer pacer;
    AudioOutput audio;
    std::thread emuThread;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<uint32_t> packedInput{0};   // direction | control << 8 | buttons << 16, see updatePlayerInput()
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

    bool applyBootROM();
    bool applyApuBios();
    void scheduleTick(std::chrono::steady_clock::time_point now);
    void emuLoop();                      // emuThread's body
    void joinEmuThread();
    void convertFrame(QImage &dst) const;   // latched display frame -> dst (system must be idle or the caller's)
    void updateFrameBuffer();
    void updatePlayerInput();
    void fillTestPattern();
};

#endif // EMULATORWIDGET_H
