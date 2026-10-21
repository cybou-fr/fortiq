#ifndef FORTIQ_NO_SSH
#include "core/RemoteController.h"
#endif

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cstdio>

Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

int main(int argc, char *argv[])
{
    // The golden screenshot is compared at DPR=1 (UI_SPEC_V1 §6), so a
    // capture run must not be scaled by the host display.
    bool capture = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--screenshot") == 0) capture = true;
    if (capture) qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CYBOU");
    QCoreApplication::setApplicationName("FORTIQ");
    QQuickStyle::setStyle("Fusion");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption fixtureOption("reference-fixture",
        "Visual-only reference scene (sample data, fixed clock). Never production data.");
    QCommandLineOption screenshotOption("screenshot",
        "Render 1536x1024 at DPR=1, save PNG to <path> and exit.", "path");
    QCommandLineOption wallpaperOption("wallpaper", "Wallpaper image file.", "path");
    parser.addOptions({ fixtureOption, screenshotOption, wallpaperOption });
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shellFixture", parser.isSet(fixtureOption));
    engine.rootContext()->setContextProperty("shellWallpaper", parser.isSet(wallpaperOption)
        ? QUrl::fromLocalFile(QDir::current().absoluteFilePath(parser.value(wallpaperOption)))
        : QUrl());
#ifndef FORTIQ_NO_SSH
    fortiq::RemoteController controller;
    engine.rootContext()->setContextProperty("remote", &controller);
#else
    engine.rootContext()->setContextProperty("remote", QVariant::fromValue<QObject *>(nullptr));
#endif
    engine.loadFromModule("Fortiq.App", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    if (parser.isSet(screenshotOption)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window) return 1;
        window->setFlag(Qt::FramelessWindowHint);
        window->setPosition(0, 0);
        window->resize(1536, 1024);
        const QString path = parser.value(screenshotOption);
        // Give layered effects a few frames to settle before grabbing.
        QTimer::singleShot(1500, &app, [window, path] {
            QImage image = window->grabWindow();
            if (image.size() != QSize(1536, 1024)) {
                std::fprintf(stderr, "capture size %dx%d, expected 1536x1024\n", image.width(), image.height());
                QCoreApplication::exit(2);
                return;
            }
            QCoreApplication::exit(image.save(path, "PNG") ? 0 : 3);
        });
    }
    return app.exec();
}
