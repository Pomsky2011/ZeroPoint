// Headless (QT_QPA_PLATFORM=offscreen) harness for the Qt frontend's real tick loop: boots a boot ROM + cartridge
// in an EmulatorWidget and prints the FPS the status bar would show, once a second.
//
//   QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy bench_qt_tick <bootrom.bin> <rom.zpb> [seconds] [window-px]
#include "emulatorwidget.h"
#include <QApplication>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <rom.zpb> [seconds] [window-px]\n", argv[0]);
        return 1;
    }
    const int seconds = argc > 3 ? std::atoi(argv[3]) : 12;
    const int px = argc > 4 ? std::atoi(argv[4]) : 512;

    EmulatorWidget w;
    w.resize(px, px);
    w.setBootROMPath(argv[1]);
    w.show();
    if (!w.loadROM(argv[2])) {
        std::fprintf(stderr, "cannot load ROM\n");
        return 1;
    }
    QObject::connect(&w, &EmulatorWidget::fpsChanged, [](int fps) { std::printf("%d FPS\n", fps); std::fflush(stdout); });
    w.start();
    QTimer::singleShot(seconds * 1000, &app, &QApplication::quit);
    return app.exec();
}
