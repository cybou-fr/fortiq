#ifndef FORTIQ_NO_SSH
#include "core/RemoteController.h"
#endif

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
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
    // Included with qt_add_resources in both the app and shell preview.
    // This is the Qt window/taskbar icon; the Windows EXE icon separately
    // comes from the generated Win32 .rc + .ico resource.
    const QIcon fortiqIcon(QStringLiteral(":/branding/fortiq_mark.png"));
    QGuiApplication::setWindowIcon(fortiqIcon);
    QQuickStyle::setStyle("Fusion");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption fixtureOption("reference-fixture",
        "Visual-only reference scene (sample data, fixed clock). Never production data.");
    QCommandLineOption screenshotOption("screenshot",
        "Render 1536x1024 at DPR=1, save PNG to <path> and exit.", "path");
    QCommandLineOption wallpaperOption("wallpaper",
        "Bundled wallpaper (aurora, desert, city), 'none' for the placeholder, or an image file.", "name|path");
    QCommandLineOption fixturePreviewOption("fixture-preview",
        "With --reference-fixture: show the file viewer with sample text or image.", "text|image");
    parser.addOptions({ fixtureOption, screenshotOption, wallpaperOption, fixturePreviewOption });
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shellFixture", parser.isSet(fixtureOption));
    const QString fixturePreview = parser.value(fixturePreviewOption);
    engine.rootContext()->setContextProperty("shellFixturePreview",
        parser.isSet(fixtureOption) && (fixturePreview == "text" || fixturePreview == "image") ? fixturePreview : QString());
    const QString wallpaper = parser.value(wallpaperOption);
    const bool bundled = !parser.isSet(wallpaperOption)
        || QStringList{ "aurora", "desert", "city", "none" }.contains(wallpaper);
    engine.rootContext()->setContextProperty("shellWallpaperName",
        // The reference fixture keeps the placeholder: none of the bundled
        // wallpapers is the master's lake scene.
        !parser.isSet(wallpaperOption) ? (parser.isSet(fixtureOption) ? QString() : QStringLiteral("aurora"))
        : (wallpaper == "none" ? QString() : (bundled ? wallpaper : QString())));
    engine.rootContext()->setContextProperty("shellWallpaper", bundled ? QUrl()
        : QUrl::fromLocalFile(QDir::current().absoluteFilePath(wallpaper)));
#ifndef FORTIQ_NO_SSH
    fortiq::RemoteController controller;
    engine.rootContext()->setContextProperty("remote", &controller);
#else
    engine.rootContext()->setContextProperty("remote", QVariant::fromValue<QObject *>(nullptr));
#endif
    engine.loadFromModule("Fortiq.App", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    if (auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()))
        rootWindow->setIcon(fortiqIcon);

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
