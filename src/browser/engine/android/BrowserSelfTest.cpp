#include "BrowserSelfTest.h"

#include "../EngineBrowser.h"
#include "../../gateway/SshTunnel.h"
#include "../../../ssh/SshClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>

namespace fortiq::engine {

struct BrowserSelfTest::Impl {
    QQmlEngine *qml = nullptr;
    QQuickWindow *window = nullptr;
    ConnectionRequest request;
    QString base;
    QString dir;
    std::unique_ptr<EngineBrowser> browser;
    QQuickItem *view = nullptr;
    QUdpSocket udpCanary;       // the same for the WebRTC gap: STUN packets aimed at this device must NEVER arrive
    int udpDatagrams = 0;
    QTcpServer canary;          // "a service of this device": a page asking for 127.0.0.1:<port> must NEVER reach it
    int canaryConnections = 0;
    QTimer timer;
    bool canaryWired = false;
    qint64 stateSince = 0;
    enum State { Connecting, Starting, Js, Canary, Idle, Waiting, WaitWebrtc, Done } state = Connecting;
    QString waitFor;            // title prefix being waited for in Js/Canary
    QString lastLoaded;
    qint64 cmdOffset = 0;
    qint64 quietSince = 0;

    void log(const QString &line) {
        QFile f(dir + QStringLiteral("/browser-selftest-report.txt"));
        if (f.open(QIODevice::Append | QIODevice::Text)) f.write((line + QLatin1Char('\n')).toUtf8());
        qInfo().noquote() << "FORTIQ-SELFTEST" << line;
    }
    qint64 now() const { return QDateTime::currentMSecsSinceEpoch(); }
    void enter(State s) { state = s; stateSince = now(); }
    QString prop(const char *name) const { return view ? view->property(name).toString() : QString(); }

    void stats(const QString &tag) {
        const QVariantMap s = browser->gatewayStats();
        log(QStringLiteral("stats.%1=accepted:%2 blocked:%3 internalAsked:%4 internalAllowed:%5 channels:%6 authRejected:%7 bytesFromServer:%8 port:%9")
            .arg(tag).arg(s.value("accepted").toULongLong()).arg(s.value("blocked").toULongLong()).arg(s.value("internalAsked").toULongLong())
            .arg(s.value("internalAllowed").toULongLong()).arg(s.value("channelsOpened").toULongLong()).arg(s.value("authRejected").toULongLong())
            .arg(s.value("bytesFromServer").toULongLong()).arg(browser->proxyPort()));
        log(QStringLiteral("canary.connections.%1=%2").arg(tag).arg(canaryConnections));
    }
    void load(const QString &where) {
        const QString url = where.startsWith(QLatin1Char('/')) ? base + where : where;
        lastLoaded = url;
        log(QStringLiteral("load=%1").arg(url));
        view->setProperty("url", QUrl(url));
    }
    void createView() {
        QQmlComponent component(qml, "Fortiq.Engine", "EngineView");
        if (component.isError()) { log(QStringLiteral("fail=view component: %1").arg(component.errorString())); enter(Done); return; }
        QObject *o = component.createWithInitialProperties({ { "engine", QVariant::fromValue<QObject *>(browser.get()) } });
        view = qobject_cast<QQuickItem *>(o);
        if (!view) { log(QStringLiteral("fail=view not created")); enter(Done); return; }
        view->setParentItem(window->contentItem());
        view->setPosition(QPointF(0, 80));
        view->setSize(QSizeF(window->width(), window->height() - 80));
        enter(Starting);
    }
    void tick() {
        if (browser && !browser->pendingConfirm().isEmpty()) {
            const QVariantMap q = browser->pendingConfirm();
            // Only the fixture's own web server is opened; every other internal address the canary asks for (127.0.0.1/::1/localhost of
            // the SERVER on the canary's port, 203.0.113.9) is refused, like the user pressing "Annuler".
            const QUrl b(base);
            const bool ours = q.value("host").toString() == b.host() && q.value("port").toInt() == b.port(80);
            log(QStringLiteral("confirm=%1:%2 -> %3").arg(q.value("host").toString()).arg(q.value("port").toInt()).arg(ours ? "allow" : "deny"));
            browser->answerConfirm(ours);
        }
        const qint64 age = now() - stateSince;
        switch (state) {
        case Connecting:
            if (browser->state() == QLatin1String("ready")) { log(QStringLiteral("gateway=ready port:%1").arg(browser->proxyPort())); createView(); }
            else if (age > 40000) { log(QStringLiteral("fail=gateway not ready: %1 %2").arg(browser->state(), browser->status())); enter(Done); }
            break;
        case Starting:
            if (view->property("started").toBool()) { log(QStringLiteral("webview=started")); load(QStringLiteral("/js")); waitFor = QStringLiteral("js-done"); enter(Js); }
            else if (!prop("failure").isEmpty()) { log(QStringLiteral("webview=refused: %1").arg(prop("failure"))); enter(Idle); }
            else if (age > 30000) { log(QStringLiteral("fail=webview did not start")); enter(Done); }
            break;
        case Js:
            if (prop("title") == waitFor) {
                log(QStringLiteral("js=OK title=%1").arg(prop("title")));
                stats(QStringLiteral("js"));
                if (!canary.listen(QHostAddress::Any, 0)) log(QStringLiteral("canary listener failed"));
                if (!canaryWired) {
                    canaryWired = true;
                    QObject::connect(&canary, &QTcpServer::newConnection, &canary, [this] { ++canaryConnections; while (auto *s = canary.nextPendingConnection()) s->deleteLater(); });
                }
                load(QStringLiteral("/canary?t=1&cp=%1").arg(canary.serverPort()));
                waitFor = QStringLiteral("canary-done:");
                enter(Canary);
            } else if (age > 60000) { log(QStringLiteral("js=TIMEOUT title=%1 failure=%2").arg(prop("title"), prop("failure"))); enter(Idle); }
            break;
        case Canary:
            if (prop("title").startsWith(waitFor)) {
                log(QStringLiteral("canary=%1").arg(prop("title").mid(waitFor.size())));
                stats(QStringLiteral("canary"));
                log(QStringLiteral("gatewayLog.begin"));
                for (const QString &l : browser->gatewayLog()) log(QStringLiteral("  ") + l);
                log(QStringLiteral("gatewayLog.end"));
                enter(Idle);
            } else if (age > 120000) { log(QStringLiteral("canary=TIMEOUT title=%1").arg(prop("title"))); enter(Idle); }
            break;
        case Waiting:
            // The page is quiet when it has not been loading for 3 s (a script may still fetch after the load event).
            if (view->property("loading").toBool() || age < 1000) quietSince = 0;
            else if (quietSince == 0) quietSince = now();
            if (quietSince != 0 && now() - quietSince > 3000) {
                log(QStringLiteral("result for=%1 url=%2 title=%3 failure=%4").arg(lastLoaded, prop("url"), prop("title"), prop("failure")));
                stats(QStringLiteral("cmd"));
                enter(Idle);
            } else if (age > 60000) { log(QStringLiteral("result=TIMEOUT for=%1 url=%2 failure=%3").arg(lastLoaded, prop("url"), prop("failure"))); enter(Idle); }
            break;
        case WaitWebrtc:
            if (prop("title").startsWith(waitFor) || age > 60000) {
                log(QStringLiteral("webrtc.via=%1").arg(lastLoaded.contains("via=frames") ? "frames" : "contentWindow"));
                log(QStringLiteral("webrtc=%1 title=%2 datagrams=%3").arg(prop("title").startsWith(waitFor) ? "done" : "TIMEOUT", prop("title")).arg(udpDatagrams));
                stats(QStringLiteral("webrtc"));
                enter(Idle);
            }
            break;
        case Idle: pollCommands(); break;
        case Done: pollCommands(); break;
        }
    }
    void pollCommands() {
        QFile f(dir + QStringLiteral("/browser-selftest-cmd.txt"));
        if (!f.open(QIODevice::ReadOnly)) return;
        f.seek(cmdOffset);
        const QByteArray data = f.readAll();
        cmdOffset = f.size();
        for (const QByteArray &raw : data.split('\n')) {
            const QString cmd = QString::fromUtf8(raw).trimmed();
            if (cmd.isEmpty()) continue;
            log(QStringLiteral("cmd=%1").arg(cmd));
            if (cmd.startsWith(QLatin1String("load ")) && view) { quietSince = 0; load(cmd.mid(5)); enter(Waiting); }
            else if (cmd == QLatin1String("gateway-stop")) { browser->setWindowActive(false); log(QStringLiteral("gateway=stopped state:%1").arg(browser->state())); }
            else if ((cmd == QLatin1String("webrtc") || cmd == QLatin1String("webrtc-frames")) && view) {
                // The page creates an RTCPeerConnection through an about:blank iframe (the gap of the document-start script) with STUN
                // servers on THIS device (every local IPv4 address, this listener's port) and on a documentation address.
                udpDatagrams = 0;
                if (!udpCanary.isOpen()) {
                    udpCanary.bind(QHostAddress::AnyIPv4, 0);
                    QObject::connect(&udpCanary, &QUdpSocket::readyRead, &udpCanary, [this] { while (udpCanary.hasPendingDatagrams()) { udpCanary.readDatagram(nullptr, 0); ++udpDatagrams; } });
                }
                QStringList ips{ QStringLiteral("127.0.0.1") };
                for (const QHostAddress &a : QNetworkInterface::allAddresses())
                    if (a.protocol() == QAbstractSocket::IPv4Protocol && !ips.contains(a.toString())) ips << a.toString();
                log(QStringLiteral("webrtc.listener=%1 ips=%2").arg(udpCanary.localPort()).arg(ips.join(',')));
                load(QStringLiteral("/webrtc?udp=%1&ips=%2&via=%3&t=1").arg(udpCanary.localPort()).arg(ips.join(',')).arg(cmd == QLatin1String("webrtc") ? "contentWindow" : "frames"));
                waitFor = QStringLiteral("webrtc-done");
                enter(WaitWebrtc);
            }
            else if (cmd == QLatin1String("stats")) stats(QStringLiteral("now"));
            else if (cmd == QLatin1String("quit")) { QCoreApplication::quit(); }
        }
    }
};

BrowserSelfTest::BrowserSelfTest(QQmlEngine *engine, QQuickWindow *window, QObject *parent) : QObject(parent), d(new Impl) {
    d->qml = engine;
    d->window = window;
}

BrowserSelfTest::~BrowserSelfTest() { delete d; }

void BrowserSelfTest::start() {
    d->dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d->dir);
    QFile::remove(d->dir + QStringLiteral("/browser-selftest-report.txt"));
    QFile::remove(d->dir + QStringLiteral("/browser-selftest-cmd.txt"));
    QFile cfg(d->dir + QStringLiteral("/browser-selftest.json"));
    if (!cfg.open(QIODevice::ReadOnly)) { d->log(QStringLiteral("fail=no config at %1").arg(cfg.fileName())); return; }
    const QJsonObject o = QJsonDocument::fromJson(cfg.readAll()).object();
    d->request.host = o.value("host").toString();
    d->request.port = o.value("port").toInt();
    d->request.user = o.value("user").toString();
    d->request.keyPath = d->dir + QLatin1Char('/') + o.value("keyFile").toString();
    d->request.expectedHostFingerprint = o.value("fingerprint").toString();
    d->base = o.value("base").toString();
    EngineBrowser::Source src;
    src.connected = [] { return true; };
    src.label = [this] { return d->request.host; };
    src.token = [] { return 1; };
    src.makeTunnel = [this](QObject *parent) -> gateway::TunnelBackend * { return new gateway::SshTunnel(d->request, parent); };
    d->browser = std::make_unique<EngineBrowser>(src);
    d->log(QStringLiteral("selftest=start sdk-webview-engine=%1").arg(EngineBrowser::compiledIn()));
    d->browser->setWindowActive(true);
    d->enter(Impl::Connecting);
    d->timer.setInterval(400);
    connect(&d->timer, &QTimer::timeout, this, [this] { d->tick(); });
    d->timer.start();
}

}  // namespace fortiq::engine
