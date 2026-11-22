// test_qt_save: the Qt frontend's .sav handling on the offscreen platform (QT_QPA_PLATFORM=offscreen), with the emulation thread
// running for real (docs/cart-save.md): the file is created next to the ROM, picked up on the next load, flushed before a yank
// and a swap and when the machine stops, never written with saves off, and a recording takes the save as its start without
// writing it back.
//
//   test_qt_save <workdir>
#include "emulatorwidget.h"
#include "movie.h"
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

// An unsigned cartridge with 2 KiB of save RAM that stores `value` into SRAM[index] in a loop:
//   lda $00:0020 ; sta $C0:00<index> ; jmp $00:0000      and the byte at ROM offset $20 is the value
void writeCart(const QString& path, uint8_t index, uint8_t value, const char* title) {
    std::vector<uint8_t> prog(0x21, 0xEA);
    const uint8_t code[] = {0x3D, 0x20, 0x00, 0x00, 0x55, index, 0x00, 0xC0, 0x10, 0x00, 0x00, 0x00};
    std::memcpy(prog.data(), code, sizeof code);
    prog[0x20] = value;
    std::vector<uint8_t> img(64, 0);
    std::memcpy(img.data(), "ZPB\0", 4);
    img[4] = 1;
    img[5] = 2 << 4;   // save RAM: 512 << 2 = 2 KiB
    img[6] = 64;
    img[8] = (uint8_t)prog.size();
    std::snprintf(reinterpret_cast<char*>(&img[0x10]), 32, "%s", title);
    img.insert(img.end(), prog.begin(), prog.end());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(reinterpret_cast<const char*>(img.data()), (qint64)img.size());
}

QByteArray slurp(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void put(const QString& path, const QByteArray& d) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(d);
}

}  // namespace

int main(int argc, char** argv) {
    setenv("ZEROPOINT_NO_VERIFY", "1", 1);   // the test cartridges are unsigned
    QApplication app(argc, argv);
    if (argc < 2) { std::fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
    const QString work = argv[1];
    const QString a = work + "/qta.zpb", b = work + "/qtb.zpb";
    const QString sa = work + "/qta.sav", sb = work + "/qtb.sav";
    writeCart(a, 3, 0xA3, "qta");
    writeCart(b, 5, 0xB5, "qtb");
    QFile::remove(sa);
    QFile::remove(sb);
    QString error;

    std::printf("a new cartridge\n");
    {
        EmulatorWidget w;
        check(w.loadROM(a), "loads");
        w.start();
        pump(400);
        w.stop();   // flushes
        const QByteArray f = slurp(sa);
        check(f.size() == 2048, "qta.sav is written next to the ROM, 2048 bytes");
        check(f.size() == 2048 && (uint8_t)f[3] == 0xA3 && (uint8_t)f[4] == 0xFF, "the guest's store is in it, the rest is blank");
    }

    std::printf("the next load picks it up\n");
    {
        QByteArray d(2048, '\xFF');
        d[100] = 0x42;
        d[3] = 0x11;
        put(sa, d);
        EmulatorWidget w;
        check(w.loadROM(a), "loads");
        w.start();
        pump(400);
        w.stop();
        const QByteArray f = slurp(sa);
        check(f.size() == 2048 && (uint8_t)f[100] == 0x42, "the saved bytes were loaded (and survive)");
        check(f.size() == 2048 && (uint8_t)f[3] == 0xA3, "the guest overwrote its own byte");
    }

    std::printf("a wrong-size file\n");
    {
        put(sa, QByteArray(100, '\x77'));
        EmulatorWidget w;
        check(w.loadROM(a), "loads");
        w.start();
        pump(300);
        w.stop();
        check(slurp(sa) == QByteArray(100, '\x77'), "is never overwritten");
        QFile::remove(sa);
    }

    std::printf("saves off\n");
    {
        EmulatorWidget w;
        w.setSavesEnabled(false);
        check(w.loadROM(a), "loads");
        w.start();
        pump(300);
        w.stop();
        check(!QFile::exists(sa), "nothing is written");
    }

    std::printf("yank and swap while running\n");
    {
        EmulatorWidget w;
        check(w.loadROM(a), "loads");
        w.start();
        pump(300);
        w.yankCartridge();
        pump(300);
        check(slurp(sa).size() == 2048 && (uint8_t)slurp(sa)[3] == 0xA3, "a yank flushes the cartridge's save first");
        QFile::remove(sa);
        check(w.loadROM(a), "loads again");
        w.start();
        pump(300);
        QString swapError;
        check(w.swapCartridge(b, &swapError), swapError.isEmpty() ? "swap accepted" : qPrintable(swapError));
        pump(400);
        check(slurp(sa).size() == 2048 && (uint8_t)slurp(sa)[3] == 0xA3, "the swap flushed the old cartridge's save");
        w.stop();
        check(slurp(sb).size() == 2048 && (uint8_t)slurp(sb)[5] == 0xB5, "the swapped-in cartridge has its own .sav");
    }

    std::printf("a recording\n");
    {
        QByteArray d(2048, '\xFF');
        d[200] = 0x42;
        put(sa, d);
        const QByteArray before = d;
        EmulatorWidget w;
        check(w.loadROM(a), "loads");
        check(w.startRecording(work + "/qtsave.zpm", "test_qt_save", &error), error.isEmpty() ? "recording starts" : qPrintable(error));
        w.start();
        pump(400);
        const QString summary = w.finishMovie();
        check(summary.startsWith("Saved"), "saved the movie");
        check(slurp(sa) == before, "the recording did not write the .sav");
        ZeroPoint::Movie m;
        std::string err;
        check(ZeroPoint::Movie::load((work + "/qtsave.zpm").toStdString(), m, err) && m.roms.size() == 1 && m.roms[0].save.size() == 2048 &&
                  m.roms[0].save[200] == 0x42,
              "the movie carries the .sav as its starting save");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
