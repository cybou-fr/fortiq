// Real screenshot of the running application (website hero, docs). Not a fixture: this is the production QML shell with the production
// controllers, connected over SSH to a real (disposable) Linux server; every pixel of the windows is data that server returned.
//
//   fortiq_live_capture --host 127.0.0.1 --port 2222 --user fqshell --fingerprint SHA256:... --out shot.png
//                       [--key <private key file>] [--demo-dir /home/fqshell/demo] [--width 1536 --height 1024]
//   the password (or key passphrase) comes from the environment: FORTIQ_CAPTURE_PASSWORD
//
// It uses its own settings scope (organization "CYBOU-capture"), so it never reads or writes the user's saved servers or pins.
// tool/capture_app_screenshot.py prepares the server (demo folder) and runs this program. Never part of ctest and never shipped.
#include "browser/BrowserController.h"
#include "core/DisksController.h"
#include "core/DocumentsController.h"
#include "core/LogsController.h"
#include "core/RemoteController.h"
#include "core/TaskManager.h"
#include "core/TerminalController.h"
#include "db/DbController.h"
#include "mail/MailController.h"
#include "network/NetworkController.h"
#include "system/SystemController.h"

#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>

#include <cstdio>

Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

namespace {
void waitFor(const std::function<bool()> &condition, int timeoutMs, const char *what) {
    QElapsedTimer t;
    t.start();
    while (!condition()) {
        if (t.elapsed() > timeoutMs) { std::fprintf(stderr, "timeout waiting for %s\n", what); std::exit(3); }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}
void pause(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}
}  // namespace

int main(int argc, char *argv[]) {
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");   // DPR 1: the picture is exactly the requested size
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CYBOU-capture");
    QCoreApplication::setApplicationName("FORTIQ");
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/fortiq_mark.png")));
    QQuickStyle::setStyle("Fusion");

    QCommandLineParser p;
    p.addHelpOption();
    for (const char *name : { "host", "port", "user", "fingerprint", "key", "out", "demo-dir", "width", "height", "wallpaper" })
        p.addOption(QCommandLineOption(QString::fromLatin1(name), QString(), QStringLiteral("value")));
    p.process(app);
    const QString out = p.value("out");
    if (out.isEmpty() || p.value("host").isEmpty() || p.value("user").isEmpty() || p.value("fingerprint").isEmpty()) {
        std::fprintf(stderr, "usage: fortiq_live_capture --host H --port P --user U --fingerprint SHA256:... --out file.png [--key file] [--demo-dir dir]\n");
        return 2;
    }
    const QString host = p.value("host");
    const int port = p.value("port").isEmpty() ? 22 : p.value("port").toInt();
    const int width = p.value("width").isEmpty() ? 1536 : p.value("width").toInt();
    const int height = p.value("height").isEmpty() ? 1024 : p.value("height").toInt();
    const QString password = qEnvironmentVariable("FORTIQ_CAPTURE_PASSWORD");

    // The host pin is what the user would have confirmed on the first connection (same key as RemoteController::pinKey()).
    QSettings settings;
    settings.setValue(QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
        (host.toLower() + ':' + QString::number(port)).toUtf8(), QCryptographicHash::Sha256).toHex()), p.value("fingerprint"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shellFixture", false);
    engine.rootContext()->setContextProperty("shellFixturePreview", QString());
    const QString wallpaper = p.value("wallpaper").isEmpty() ? QStringLiteral("aurora") : p.value("wallpaper");
    engine.rootContext()->setContextProperty("shellWallpaperName", wallpaper);
    engine.rootContext()->setContextProperty("shellWallpaper", QUrl());
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

    engine.loadFromModule("Fortiq.App", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) return 1;
    window->setFlag(Qt::FramelessWindowHint);
    window->setPosition(0, 0);
    window->resize(width, height);
    auto *shell = window->findChild<QObject *>(QStringLiteral("desktopShell"));
    if (!shell) { std::fprintf(stderr, "no desktopShell\n"); return 1; }
    pause(800);

    controller.connectServer(host, port, p.value("user"), password, p.value("key"));
    waitFor([&] { return controller.connected(); }, 30000, "the SSH connection");
    const QString demo = p.value("demo-dir");
    if (!demo.isEmpty()) {
        controller.navigate(demo);
        waitFor([&] { return controller.currentDirectory() == demo && !controller.busy(); }, 20000, "the demo folder listing");
    }
    shell->setProperty("filesOpen", true);
    shell->setProperty("terminalOpen", true);
    pause(1500);
    waitFor([&] { return terminal.running(); }, 30000, "the terminal session");
    // Composition: Files at the upper left, Terminal at the lower right, the running-terminal task toast hidden.
    if (auto *files = window->findChild<QQuickItem *>(QStringLiteral("filesWindow"))) { files->setX(310); files->setY(92); files->setHeight(470); }
    if (auto *term = window->findChild<QQuickItem *>(QStringLiteral("terminalWindow"))) { term->setX(790); term->setY(510); term->setWidth(700); term->setHeight(390); }
    if (auto *toast = window->findChild<QQuickItem *>(QStringLiteral("taskPanel"))) toast->setOpacity(0);
    terminal.send(QByteArray("PS1='\\u@debian:\\w\\$ '; cd '") + demo.toUtf8() + "'; clear; uname -sr; echo; ls --color=always -F\r");
    pause(2500);
    shell->setProperty("foregroundApp", "files");
    pause(1500);

    QImage image = window->grabWindow();
    if (image.size() != QSize(width, height)) {
        std::fprintf(stderr, "capture size %dx%d, expected %dx%d\n", image.width(), image.height(), width, height);
        return 2;
    }
    if (!image.save(out, "PNG")) return 4;
    std::printf("saved %s (%dx%d)\n", qPrintable(out), image.width(), image.height());
    terminal.send("exit\r");
    controller.disconnectServer();
    return 0;
}
