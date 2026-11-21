// test_qt_serial: the Qt frontend's debug serial port on the offscreen platform (QT_QPA_PLATFORM=offscreen), with the
// emulation thread running for real. A dev-unit machine running ZPdevtools' serial_echo program must show its banner,
// echo what serialSend() types, mirror both to a --serial-out style file, and survive record -> playback with the same
// output; a retail machine must show nothing and take no input.
//
//   test_qt_serial <workdir> <echo.zpb>
#include "emulatorwidget.h"
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Pump until `got` is at least `want` bytes or the time is up (a console second is cheap; 400 ms of wall time is plenty).
void pumpUntil(const QByteArray& got, int want, int maxMs) {
    for (int waited = 0; waited < maxMs && got.size() < want; waited += 20) pump(20);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 3) { std::fprintf(stderr, "usage: %s <workdir> <echo.zpb>\n", argv[0]); return 2; }
    const QString work = argv[1], rom = argv[2];
    const QByteArray banner = "Hello from ZeroPoint!\n";
    QString error;

    std::printf("retail unit\n");
    {
        EmulatorWidget w;
        QByteArray got;
        QObject::connect(&w, &EmulatorWidget::serialOutput, [&](const QByteArray& b) { got += b; });
        check(w.loadROM(rom), "cartridge loads");
        w.start();
        w.serialSend("nope");
        pump(300);
        w.stop();
        check(got.isEmpty(), "no serial output on a retail unit");
    }

    std::printf("dev unit\n");
    const QString outFile = work + "/qt_serial_out.txt";
    QByteArray live;
    {
        EmulatorWidget w;
        QByteArray got;
        QString failed;
        QObject::connect(&w, &EmulatorWidget::serialOutput, [&](const QByteArray& b) { got += b; });
        QObject::connect(&w, &EmulatorWidget::serialSetupFailed, [&](const QString& m) { failed = m; });
        w.setDevUnit(true);
        w.setSerialEndpoints({outFile}, {}, -1);
        check(w.loadROM(rom), "cartridge loads on a dev unit");
        w.start();
        pumpUntil(got, banner.size(), 3000);
        check(got == banner, "the banner reaches the console");
        w.serialSend("ping");
        pumpUntil(got, banner.size() + 4, 3000);
        check(got == banner + "ping", "typed bytes are echoed back");
        w.stop();
        check(failed.isEmpty(), "the output file opened");
        live = got;
    }
    {
        QFile f(outFile);
        check(f.open(QIODevice::ReadOnly) && f.readAll() == live, "--serial-out file holds the same bytes");
    }

    std::printf("record and replay\n");
    {
        const QString moviePath = work + "/qt_serial.zpm";
        QByteArray recorded;
        {
            EmulatorWidget w;
            QObject::connect(&w, &EmulatorWidget::serialOutput, [&](const QByteArray& b) { recorded += b; });
            w.setDevUnit(true);
            check(w.loadROM(rom), "cartridge loads");
            check(w.startRecording(moviePath, "test_qt_serial", &error), error.isEmpty() ? "recording starts" : qPrintable(error));
            w.start();
            pumpUntil(recorded, banner.size(), 3000);
            w.serialSend("ping");
            pumpUntil(recorded, banner.size() + 4, 3000);
            const QString summary = w.finishMovie();
            check(recorded == banner + "ping", "recorded session echoed the typed bytes");
            check(summary.startsWith("Saved"), qPrintable(summary));
        }
        {
            EmulatorWidget w;   // note: dev unit NOT set here; the movie decides
            QByteArray played;
            bool ended = false, ok = false;
            QObject::connect(&w, &EmulatorWidget::serialOutput, [&](const QByteArray& b) { played += b; });
            QObject::connect(&w, &EmulatorWidget::movieEnded, [&](const QString&, bool good) { ended = true; ok = good; });
            check(w.startPlayback(moviePath, {rom}, &error), error.isEmpty() ? "playback starts" : qPrintable(error));
            w.start();
            for (int waited = 0; waited < 20000 && !ended; waited += 20) pump(20);
            w.finishMovie();
            check(ended && ok, "playback finished with no desync");
            check(played == recorded, "replayed output is byte-identical");
        }
    }

    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
