#include "core/RemoteController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CYBOU");
    QCoreApplication::setApplicationName("FORTIQ");
    QQuickStyle::setStyle("Fusion");

    fortiq::RemoteController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("remote", &controller);
    engine.loadFromModule("Fortiq.App", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
