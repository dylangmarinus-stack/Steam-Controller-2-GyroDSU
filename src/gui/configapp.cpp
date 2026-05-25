#include "mainwindow.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("sc2gyrodsu-config");
    app.setApplicationDisplayName("SteamControllerGyroDSU Configuration");
    app.setApplicationVersion("1.0");

    // Use the embedded SC2026 icon; fall back to the installed theme icon.
    QIcon appIcon = QIcon(":/icons/sc2gyrodsu.png");
    if (appIcon.isNull())
        appIcon = QIcon::fromTheme("sc2gyrodsu", QIcon::fromTheme("input-gamepad"));
    app.setWindowIcon(appIcon);

    sc2::MainWindow win;
    win.show();

    return app.exec();
}
