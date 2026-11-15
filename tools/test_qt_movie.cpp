// test_qt_movie: the Qt frontend's movie plumbing on the offscreen platform (QT_QPA_PLATFORM=offscreen), with the
// emulation thread running for real: record with key presses and every host action issued from the GUI thread, save,
// then play the file back and check it finishes with every checkpoint verified.
//
//   test_qt_movie <workdir> <rom.zpb> <other.zpb>
#include "emulatorwidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QTimer>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

void pump(int ms) {   // let the emulation thread run while the GUI event loop turns
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void key(EmulatorWidget& w, int k, bool down) {
    QKeyEvent e(down ? QEvent::KeyPress : QEvent::KeyRelease, k, Qt::NoModifier);
    QApplication::sendEvent(&w, &e);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 4) { std::fprintf(stderr, "usage: %s <workdir> <rom.zpb> <other.zpb>\n", argv[0]); return 2; }
    const QString work = argv[1], rom = argv[2], other = argv[3];
    const QString moviePath = work + "/qt.zpm";
    QString error;

    std::printf("record\n");
    {
        EmulatorWidget w;
        w.setTwoPlayers(true);
        check(w.loadROM(rom), "cartridge loads");
        check(w.startRecording(moviePath, "test_qt_movie", &error), error.isEmpty() ? "recording starts" : qPrintable(error));
        w.start();
        check(w.isRecording(), "the widget reports it is recording");
        pump(250);
        key(w, Qt::Key_Up, true);  pump(120);                       // player 1
        key(w, Qt::Key_I, true);   pump(120);                       // player 2
        key(w, Qt::Key_Z, true);   pump(120);
        w.reset();                 pump(200);                       // soft reset (a logged event)
        key(w, Qt::Key_Up, false); key(w, Qt::Key_Z, false); pump(120);
        w.yankCartridge();         pump(200);
        QString swapError;
        check(w.swapCartridge(other, &swapError), swapError.isEmpty() ? "swap request accepted" : qPrintable(swapError));
        pump(250);
        w.hardReset();             pump(250);
        key(w, Qt::Key_I, false);  pump(100);
        const QString summary = w.finishMovie();
        std::printf("      (%s)\n", qPrintable(summary));
        check(summary.startsWith("Saved"), "finishMovie() saved the recording");
        check(!w.isRecording(), "and the widget is no longer recording");
    }

    std::printf("playback\n");
    {
        EmulatorWidget w;
        QString ended;
        bool endedOk = false, got = false;
        QObject::connect(&w, &EmulatorWidget::movieEnded, [&](const QString& s, bool ok) { ended = s; endedOk = ok; got = true; });
        check(w.startPlayback(moviePath, QStringList() << work, &error), error.isEmpty() ? "playback starts" : qPrintable(error));
        w.start();
        check(w.isPlayingMovie(), "the widget reports it is playing a movie");
        // Keys must do nothing now: the movie is in control.
        key(w, Qt::Key_Down, true);
        QElapsedTimer t;
        t.start();
        while (!got && t.elapsed() < 20000) pump(50);
        check(got, "playback ended by itself");
        check(endedOk, ended.toUtf8().constData());
        std::printf("      (%s)\n", qPrintable(ended));
        const QString summary = w.finishMovie();
        check(summary.startsWith("Movie finished"), "finishMovie() reports a clean finish");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
