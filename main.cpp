#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "src/AnalyzerInterface.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // Register our Interface
    qmlRegisterType<AnalyzerInterface>("AudioAnalyzer", 1, 0, "AnalyzerInterface");
    qmlRegisterType<MicrophoneInput>("AudioAnalyzer", 1, 0, "MicrophoneInput");

    engine.loadFromModule("AudioAnalyzer", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
