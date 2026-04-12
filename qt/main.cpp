#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setOrganizationName("ZeroPoint");
    QApplication::setOrganizationDomain("zeropoint.emu");
    QApplication::setApplicationName("ZeroPoint Emulator");

    MainWindow window;
    window.show();

    return app.exec();
}
