#ifndef FORTIQ_NO_SSH
#include "core/LogsController.h"
#include "core/RemoteController.h"
#include "core/TerminalController.h"
#include "core/DisksController.h"
#include "core/DocumentsController.h"
#include "core/SecretStore.h"
#include "core/TaskManager.h"
#include "browser/BrowserController.h"
#include "browser/engine/EngineBrowser.h"
#include "network/NetworkController.h"
#include "system/SystemController.h"
#include "db/DbController.h"
#include "mail/MailController.h"
#include <QSettings>
#include <QUuid>
#endif

#include "core/BackNavigationFilter.h"

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

#ifdef FORTIQ_HAVE_WEBENGINE
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif
#ifdef FORTIQ_ANDROID_BROWSER_SELFTEST
#include "browser/engine/android/BrowserSelfTest.h"
#endif

Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)
#if defined(FORTIQ_HAVE_WEBENGINE) || defined(FORTIQ_HAVE_ANDROID_WEBVIEW)
Q_IMPORT_QML_PLUGIN(Fortiq_EnginePlugin)
#endif

#ifndef FORTIQ_NO_SSH
// Developer diagnostic (`--secret-store-selftest`): exercises the protected
// secret store of this platform with a throw-away entry and reports PASS/FAIL
// on stdout and in the log (logcat on Android). Touches no saved server.
static bool secretStoreSelfTest(QString &report) {
    using fortiq::SecretStore;
    const QString id = QStringLiteral("selftest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString other = id + QStringLiteral("-other");
    const QString secret = QString::fromUtf8("p\u00e4ss w\u00f6rd \u041f\u0440\u0438\u0432\u0435\u0442 \"q\" 'x' $(y)");
    auto fail = [&](const QString &why) { report = QStringLiteral("FAIL: ") + why; SecretStore::remove(id); SecretStore::remove(other); return false; };
    if (!SecretStore::available()) { report = QStringLiteral("UNAVAILABLE: ") + SecretStore::unavailableReason(); return false; }
    if (!SecretStore::load(id).notFound) return fail(QStringLiteral("unexpected entry before save"));
    if (const auto s = SecretStore::save(id, secret); !s.ok) return fail(QStringLiteral("save: ") + s.error);
    const auto loaded = SecretStore::load(id);
    if (!loaded.ok || loaded.secret != secret) return fail(QStringLiteral("round trip mismatch: ") + loaded.error);
#ifdef Q_OS_ANDROID
    {   // The blob is bound to its server id and protected against tampering.
        QSettings settings;
        const QString k = QStringLiteral("secrets/") + id;
        const QByteArray blob = settings.value(k).toByteArray();
        if (blob.isEmpty()) return fail(QStringLiteral("no ciphertext stored"));
        if (QByteArray::fromBase64(blob).contains(secret.toUtf8())) return fail(QStringLiteral("plaintext found in stored blob"));
        settings.setValue(QStringLiteral("secrets/") + other, blob);
        if (SecretStore::load(other).ok) return fail(QStringLiteral("blob accepted under another server id"));
        QByteArray raw = QByteArray::fromBase64(blob);
        raw[raw.size() - 1] = char(raw.at(raw.size() - 1) ^ 0x01);
        settings.setValue(k, raw.toBase64());
        if (SecretStore::load(id).ok) return fail(QStringLiteral("tampered blob accepted"));
    }
#endif
    if (!SecretStore::remove(id).ok || !SecretStore::load(id).notFound) return fail(QStringLiteral("remove"));
    SecretStore::remove(other);
    report = QStringLiteral("PASS");
    return true;
}
#endif

int main(int argc, char *argv[])
{
    // The golden screenshot is compared at DPR=1 (UI_SPEC_V1 §6), so a
    // capture run must not be scaled by the host display.
    bool capture = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--screenshot") == 0) capture = true;
    if (capture) qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
#ifdef FORTIQ_HAVE_WEBENGINE
    QtWebEngineQuick::initialize();   // must come before the application object (shared GL contexts)
#endif

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
        "With --reference-fixture: show the file viewer (text, image) or the transfer queue with samples.", "text|image|editor|transfers|trash|trashview|search|logs|incident|service|panelleft");
    QCommandLineOption secretSelfTestOption("secret-store-selftest",
        "Developer diagnostic: test the protected secret store with a throw-away entry, print the result and exit.");
    QCommandLineOption browserSelfTestOption("browser-selftest", "Developer-only Android Browser self-test (builds with FORTIQ_ANDROID_BROWSER_SELFTEST).");
    parser.addOptions({ fixtureOption, screenshotOption, wallpaperOption, fixturePreviewOption, secretSelfTestOption, browserSelfTestOption });
    parser.process(app);
#ifndef FORTIQ_NO_SSH
    if (parser.isSet(secretSelfTestOption)) {
        QString report;
        const bool ok = secretStoreSelfTest(report);
        qInfo().noquote() << "SECRETSTORE-SELFTEST" << report;
        std::printf("SECRETSTORE-SELFTEST %s\n", qPrintable(report));
        return ok ? 0 : 4;
    }
#endif

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shellFixture", parser.isSet(fixtureOption));
    const QString fixturePreview = parser.value(fixturePreviewOption);
    engine.rootContext()->setContextProperty("shellFixturePreview",
        parser.isSet(fixtureOption) && QStringList{ "text", "image", "editor", "transfers", "trash", "trashview", "search", "logs", "incident", "service", "panelleft" }.contains(fixturePreview) ? fixturePreview : QString());
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
    // One task model for every app; it outlives the controllers that report to it.
    fortiq::TaskManager taskManager;
    engine.rootContext()->setContextProperty("tasks", &taskManager);
    fortiq::RemoteController controller;
    controller.setTaskManager(&taskManager);
    engine.rootContext()->setContextProperty("remote", &controller);
    fortiq::LogsController logs(&controller);
    engine.rootContext()->setContextProperty("logs", &logs);
    fortiq::DocumentsController documents(&controller);
    engine.rootContext()->setContextProperty("documents", &documents);
    fortiq::DisksController disks(&controller);
    engine.rootContext()->setContextProperty("disks", &disks);
    fortiq::BrowserController browser(&controller);
    engine.rootContext()->setContextProperty("browser", &browser);
    fortiq::engine::EngineBrowser engineBrowser(&controller);
    engine.rootContext()->setContextProperty("engineBrowser", &engineBrowser);
    fortiq::NetworkController network(&controller);
    engine.rootContext()->setContextProperty("network", &network);
    fortiq::SystemController systemInfo(&controller);
    engine.rootContext()->setContextProperty("systemInfo", &systemInfo);
    fortiq::MailController mail(&controller);
    engine.rootContext()->setContextProperty("mail", &mail);
    fortiq::DbController databases(&controller);
    engine.rootContext()->setContextProperty("databases", &databases);
    fortiq::TerminalController terminal(&controller);
    engine.rootContext()->setContextProperty("terminal", &terminal);
#else
    engine.rootContext()->setContextProperty("databases", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("mail", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("tasks", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("remote", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("logs", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("terminal", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("disks", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("browser", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("engineBrowser", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("network", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("systemInfo", QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty("documents", QVariant::fromValue<QObject *>(nullptr));
#endif
    engine.loadFromModule("Fortiq.App", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    if (auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        rootWindow->setIcon(fortiqIcon);
#ifdef Q_OS_ANDROID
        auto *shell = rootWindow->findChild<QObject *>(QStringLiteral("desktopShell"));
        if (shell) new fortiq::BackNavigationFilter(rootWindow, [shell] {
            QVariant handled;
            return QMetaObject::invokeMethod(shell, "handleBack", Q_RETURN_ARG(QVariant, handled))
                && handled.toBool();
        });
#endif
    }

#ifdef FORTIQ_ANDROID_BROWSER_SELFTEST
    if (parser.isSet(browserSelfTestOption)) {
        if (auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
            auto *selfTest = new fortiq::engine::BrowserSelfTest(&engine, rootWindow, &app);
            QTimer::singleShot(1500, selfTest, [selfTest] { selfTest->start(); });
        }
    }
#endif

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
