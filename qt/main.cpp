#include "mainwindow.h"
#include <QApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setOrganizationName("ZeroPoint");
    QApplication::setOrganizationDomain("zeropoint.emu");
    QApplication::setApplicationName("ZeroPoint Emulator");

    // `zeropoint_qt [game.zpb] [--dev-unit [--serial-out FILE|-] [--serial-in FILE|-] [--serial-tcp PORT]]`: the ROM is
    // what a file manager passes for a double-clicked .zpb; the flags are zeropoint_sdl's (docs/debug-protocol.md, docs/exceptions.md).
    const QStringList args = QApplication::arguments();
    QString romPath;
    bool devUnit = false;
    QStringList serialOuts, serialIns;
    int serialTcpPort = -1;
    QString exceptionHandler;
    for (int i = 1; i < args.size(); i++) {
        const QString &a = args.at(i);
        const bool more = i + 1 < args.size();
        if (a == "--dev-unit") {
            devUnit = true;
        } else if (a == "--serial-out" && more) {
            serialOuts << args.at(++i);
        } else if (a == "--serial-in" && more) {
            serialIns << args.at(++i);
        } else if (a == "--exception-handler" && more) {
            exceptionHandler = args.at(++i);
        } else if (a == "--serial-tcp" && more) {
            bool ok = false;
            serialTcpPort = args.at(++i).toInt(&ok);
            if (!ok || serialTcpPort < 0 || serialTcpPort > 65535) {
                std::fprintf(stderr, "--serial-tcp wants a port number\n");
                return 1;
            }
        } else if (!a.startsWith('-') && romPath.isEmpty()) {
            romPath = a;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", qPrintable(a));
            std::fprintf(stderr, "usage: %s [game.zpb] [--dev-unit [--serial-out FILE|-] [--serial-in FILE|-] [--serial-tcp PORT] [--exception-handler FILE]]\n",
                         qPrintable(args.at(0)));
            return 1;
        }
    }
    if (!devUnit && (!serialOuts.isEmpty() || !serialIns.isEmpty() || serialTcpPort >= 0 || !exceptionHandler.isEmpty()))
        std::fprintf(stderr, "note: --serial-* and --exception-handler only do anything with --dev-unit\n");

    MainWindow window;
    window.setSerialOptions(devUnit, serialOuts, serialIns, serialTcpPort, exceptionHandler);
    window.show();

    // Load and start it once the event loop is up (starting emulation arms timers)
    if (!romPath.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, romPath]() { window.openROMFile(romPath, false); });
    }

    return app.exec();
}
