#include <QApplication>
#include <cstdio> // DEBUG_STARTUP

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    std::fprintf(stderr, "[DEBUG_STARTUP] main() begin\n");
    std::fflush(stderr);
    QApplication app(argc, argv);
    std::fprintf(stderr, "[DEBUG_STARTUP] QApplication created, platform=%s\n", app.platformName().toLocal8Bit().constData());
    std::fflush(stderr);

    MainWindow window;
    std::fprintf(stderr, "[DEBUG_STARTUP] MainWindow constructed\n");
    std::fflush(stderr);
    window.resize(1100, 720);
    window.show();
    std::fprintf(stderr, "[DEBUG_STARTUP] window.show() called\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[DEBUG_STARTUP] entering app.exec()\n");
    std::fflush(stderr);
    return app.exec();
}
