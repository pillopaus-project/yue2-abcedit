#include <QApplication>
#include <QByteArray>
#include <QLoggingCategory>
#include <QMessageBox>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    // The preview plays plain WAV audio: disable the FFmpeg backend's
    // hardware probing (its codec/HW lists are unrelated console noise;
    // software decoding is unaffected). Must precede any media object.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", QByteArray(","));
    // Belt and braces for any remaining backend chatter.
    QLoggingCategory::setFilterRules(
        QStringLiteral("qt.multimedia.ffmpeg=false\n"
                       "qt.multimedia.plugin=false"));
    QApplication app(argc, argv);
    yue2_abcedit::MainWindow win;
    if (argc > 1 && !win.openEditorFile(argv[1])) {
        QMessageBox::warning(&win, "Open",
                             QString("Cannot read file: %1").arg(argv[1]));
    }
    win.show();
    return app.exec();
}
