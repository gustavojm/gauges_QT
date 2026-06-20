#include "main_window.h"

#include <QApplication>

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Gauge Reader");

    MainWindow window(argv[1]);
    window.show();

    return app.exec();
}
