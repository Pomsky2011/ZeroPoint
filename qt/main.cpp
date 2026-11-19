#include "mainwindow.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setOrganizationName("ZeroPoint");
    QApplication::setOrganizationDomain("zeropoint.emu");
    QApplication::setApplicationName("ZeroPoint Emulator");

    MainWindow window;
    window.show();

    // `zeropoint_qt game.zpb` (what a file manager runs for a double-clicked .zpb): load and start it once the event
    // loop is up (starting emulation arms timers)
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && !args.at(1).startsWith('-')) {
        const QString path = args.at(1);
        QTimer::singleShot(0, &window, [&window, path]() { window.openROMFile(path, false); });
    }

    return app.exec();
}
