#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickWindow>
#include <QQuickItem>
#include <QWheelEvent>
#include <QClipboard>
#include <QDir>
#include <QQmlExtensionPlugin>
#include "../src/core/BackNavigationFilter.h"
#include "../src/core/TaskManager.h"
#ifndef Q_MOC_RUN
#include "../src/documents/MarkdownPreview.h"
#endif
Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

// The tests used to sleep a fixed time after every interaction and failed when the machine was busy (a game running, a build):
// the sleep was shorter than the work. Now they wait for conditions, with a generous bound that is only ever reached on a real failure.
#define QTRY_V(cond) QTRY_VERIFY_WITH_TIMEOUT(cond, 20000)
#define QTRY_C(actual, expected) QTRY_COMPARE_WITH_TIMEOUT(actual, expected, 20000)
// Lets the scene catch up with what the test just did: queued events are delivered and one synchronous render runs polish, bindings
// and the scene-graph sync (grabWindow). QML here has no animations, so after this the items are where they will be.
static void settle(QQuickWindow *window) {
    QCoreApplication::processEvents();
    (void)window->grabWindow();
    QCoreApplication::processEvents();
}

static QQuickItem *findByName(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems()) if (auto *hit = findByName(child, name)) return hit;
    return nullptr;
}
#ifndef Q_MOC_RUN // moc cannot parse raw strings
static const char kFakeController[] = R"(import QtQml
QtObject {
    property bool connected: false
    property bool busy: false
    property string pendingFingerprint: ""
    property string statusText: ""
    property string serverLabel: "vps-1"
    property bool secretStoreAvailable: true
    property var calls: []
    property var servers: [
        { id: "a", name: "vps-1", host: "vps.example", port: 22, user: "debian", keyPath: "", active: false, pinned: true },
        { id: "b", name: "Maison", host: "192.0.2.10", port: 2222, user: "root", keyPath: "C:/k/id_ed25519", active: false, pinned: false },
        { id: "c", name: "Coffre", host: "203.0.113.5", port: 22, user: "ops", keyPath: "", active: false, pinned: true, secretSaved: true } ]
    signal changed()
    function connectTo(id, secret) { calls = calls.concat([id + ":" + secret]) }
    function connectToRemembering(id, secret, remember) { calls = calls.concat(["remember:" + id + ":" + secret]) }
    function connectToSaved(id) { calls = calls.concat(["saved:" + id]) }
    function forgetSecret(id) { calls = calls.concat(["forget:" + id]) }
    function trustHost() {}
    function saveServer(o) { return "x" }
    function removeServer(id) {}
    function disconnectServer() { connected = false; changed() }
})";
#endif

#ifndef Q_MOC_RUN // moc cannot parse raw strings
static const char kFakeBrowser[] = R"(import QtQml
QtObject {
    property bool busy: false
    property string state: "ready"
    property string status: "HTTP 200 OK · 1,2 Ko · 0,12 s"
    property string url: "https:\u002f\u002fexample.com/docs"
    property string title: "Example"
    property string kind: "html"
    property string contentHtml: "<h1>Hello page</h1><p>Text with a <a href='https:\u002f\u002fexample.com/more'>link</a> and <b>bold</b>.</p><ul><li>one</li><li>two</li></ul>"
    property string contentText: ""
    property string imageSource: ""
    property var headers: [{ name: "content-type", value: "text/html; charset=UTF-8" }, { name: "server", value: "nginx" }]
    property var info: ({ requested: "https:\u002f\u002fexample.com/docs", status: 200, statusText: "OK", httpVersion: "2", remoteIp: "203.0.113.7",
        contentType: "text/html; charset=UTF-8", charset: "utf-8", size: 1234, redirects: 1, redirectChain: [{ status: 301, location: "https:\u002f\u002fexample.com/docs" }],
        tlsVerified: true, timings: { dns: 0.01, connect: 0.02, tls: 0.05, firstByte: 0.08, total: 0.12 } })
    property var notes: ["Mode lecture : seul le contenu principal est affiché."]
    property string refreshUrl: ""
    property bool reader: true
    property bool canGoBack: true
    property bool canGoForward: false
    property var calls: []
    function open(t) { calls = calls.concat(["open:" + t]) }
    function openLink(h) { calls = calls.concat(["link:" + h]) }
    function reload() { calls = calls.concat(["reload"]) }
    function stop() { calls = calls.concat(["stop"]) }
    function goBack() { calls = calls.concat(["back"]) }
    function goForward() { calls = calls.concat(["forward"]) }
    function setWindowActive(a) {}
    property var pendingConfirm: ({})
    function confirmInternal() { calls = calls.concat(["confirm"]); state = "ready"; pendingConfirm = ({}) }
    function cancelConfirm() { calls = calls.concat(["cancel-confirm"]); state = "ready"; pendingConfirm = ({}) }
})";
static const char kFakeNetwork[] = R"(import QtQml
QtObject {
    property bool busy: false
    property string state: 'ready'
    property string status: 'Lecture seule'
    property string updated: '12:00:00'
    property string hostname: 'srv-test'
    property var interfaces: [
        { name: 'lo', index: 1, state: 'UNKNOWN', up: true, mtu: 65536, mac: '00:00:00:00:00:00', kind: 'loopback', flags: ['UP'],
          addresses: [{ family: 'inet', address: '127.0.0.1', prefix: 8, scope: 'host', text: '127.0.0.1/8' }], hasStats: true, rx: '1.0 Mio', tx: '1.0 Mio', rxPackets: 10, txPackets: 10, errors: 0, dropped: 0 },
        { name: 'eth0', index: 2, state: 'UP', up: true, mtu: 1500, mac: '52:54:00:aa:bb:cc', kind: 'ether', flags: ['UP'],
          addresses: [{ family: 'inet', address: '192.0.2.10', prefix: 24, scope: 'global', text: '192.0.2.10/24' }, { family: 'inet6', address: 'fe80::1', prefix: 64, scope: 'link', text: 'fe80::1/64' }],
          hasStats: true, rx: '12.5 Mio', tx: '3.20 Mio', rxPackets: 900, txPackets: 800, errors: 2, dropped: 0 } ]
    property var routes: [{ family: 'inet', destination: 'default', gateway: '192.0.2.1', device: 'eth0', protocol: 'dhcp', prefsrc: '', metric: 100, isDefault: true }]
    property var listeners: [
        { protocol: 'tcp', state: 'LISTEN', address: '0.0.0.0', port: 22, process: 'sshd', pid: 700, exposure: 'all', recvQ: 0, sendQ: 128 },
        { protocol: 'tcp', state: 'LISTEN', address: '127.0.0.1', port: 5432, process: '', pid: 0, exposure: 'local', recvQ: 0, sendQ: 244 } ]
    property int exposedCount: 1
    property var notes: ['Les noms de processus ne sont visibles que pour votre utilisateur (ou root).']
    property var dnsServers: ['192.0.2.53']
    property var dnsSearch: ['example.net']
    property string dnsNote: ''
    property string resolvedText: ''
    property var tools: ['ping', 'getent']
    property bool probeBusy: false
    property string probeKind: ''
    property string probeTarget: ''
    property string probeOutput: ''
    property string probeStatus: ''
    property var calls: []
    function reload() { calls = calls.concat(['reload']) }
    function setWindowActive(a) {}
    function runProbe(k, t) { calls = calls.concat(['probe:' + k + ':' + t]); probeKind = k; probeTarget = t; probeOutput = '4 packets transmitted'; probeStatus = 'Terminé.' }
    function cancelProbe() {}
})";
static const char kFakeSystem[] = R"(import QtQml
QtObject {
    property bool busy: false
    property string state: 'ready'
    property string status: 'Lecture seule'
    property string updated: '12:00:00'
    property bool hasSystemd: true
    property var notes: []
    property var overview: ({ hostname: 'srv-test', os: 'Debian GNU/Linux 13', kernel: '6.6.0', cores: 4, load1: 0.5, load5: 1.5, load15: 2.0, loadRatio: 0.125,
        uptime: '3 j 4 h', uptimeSec: 273600, memTotal: '8.00 Gio', memUsed: '2.00 Gio', memAvailable: '6.00 Gio', memFraction: 0.25,
        swapTotal: '1.00 Gio', swapUsed: '0 Kio', swapFraction: 0, systemdState: 'running', processes: 3, running: 1, zombies: 0,
        services: 3, activeServices: 1, failedServices: 1 })
    property var processes: [
        { pid: 700, ppid: 1, user: 'www-data', state: 'Sl', cpu: 12.5, mem: 3.1, rssKb: 250000, rss: '244 Mio', elapsed: '3 h 10 min', elapsedSec: 11400, threads: 8, command: '/usr/sbin/nginx -g daemon on;', name: 'nginx', kernel: false },
        { pid: 1, ppid: 0, user: 'root', state: 'Ss', cpu: 0.0, mem: 0.1, rssKb: 9000, rss: '8.8 Mio', elapsed: '3 j 4 h', elapsedSec: 273600, threads: 1, command: '/sbin/init', name: 'init', kernel: false },
        { pid: 2, ppid: 0, user: 'root', state: 'S', cpu: 0.0, mem: 0.0, rssKb: 0, rss: '0 Kio', elapsed: '3 j 4 h', elapsedSec: 273600, threads: 1, command: '[kthreadd]', name: 'kthreadd', kernel: true } ]
    property var services: [
        { unit: 'ssh.service', load: 'loaded', active: 'active', sub: 'running', description: 'OpenBSD Secure Shell server', failed: false },
        { unit: 'broken.service', load: 'loaded', active: 'failed', sub: 'failed', description: 'A broken thing', failed: true },
        { unit: 'cron.service', load: 'loaded', active: 'inactive', sub: 'dead', description: 'Regular background program processing daemon', failed: false } ]
    property int reloads: 0
    function reload() { reloads = reloads + 1 }
    function setWindowActive(a) {}
})";
static const char kFakeRouter[] = R"(import QtQml
QtObject {
    property bool connected: true
    property string activeServerId: "srvA"
    property string serverLabel: "u@a"
    property var navigated: []
    property string trashDirectory: ""
    property var servers: []
    property bool busy: false
    property bool secretStoreAvailable: false
    property string pendingFingerprint: ""
    property string statusText: ""
    signal changed()
    function navigate(path) { navigated = navigated.concat([path]) }
})";
static const char kFakeDb[] = R"(import QtQml
QtObject {
    property bool canEdit: true
    property bool secretStoreAvailable: true
    property var profiles: [ { id: 'p1', serverId: 's', name: 'Local', engine: 'sqlite', path: '/home/u/a.db', host: '', port: 5432, user: '', database: '', hasPassword: false, summary: '/home/u/a.db' } ]
    property string selectedId: 'p1'
    property var selected: profiles[0]
    property bool busy: false
    property string status: 'Terminé · lecture seule'
    property string error: ''
    property var columns: ['id', 'name', 'score']
    property var rows: [ [ '1', 'Zoë', '1.5' ], [ '2', null, null ] ]
    property var numeric: [ true, false, true ]
    property bool truncated: false
    property string resultInfo: '2 ligne(s), 3 colonne(s)'
    property var schema: [ { schema: '', name: 'people', kind: 'table', label: 'people', columns: [ { name: 'id', type: 'INTEGER', nullable: false }, { name: 'name', type: 'TEXT', nullable: true } ] } ]
    property string schemaStatus: ''
    property var roleCheck: ({ ran: true, level: 'warning', summary: 'Ce role peut ecrire dans la base : le mode lecture seule de FORTIQ nest quune protection locale.',
        findings: ['Droit decriture sur 3 tables ou vues sur 12.'], recommendation: '-- recipe\nCREATE ROLE fortiq_ro LOGIN PASSWORD CHANGEZ-MOI;' })
    property int timeoutSec: 15
    property int maxRows: 500
    property var calls: []
    function select(id) { calls = calls.concat(['select:' + id]) }
    function runQuery(sql) { calls = calls.concat(['run:' + sql]) }
    function cancelQuery() {}
    function loadSchema() {}
    function setLimits(t, r) { calls = calls.concat(['limits:' + t + ':' + r]) }
    function selectAllFor(o) { return 'SELECT * FROM "' + o.name + '" LIMIT 100' }
    function saveProfile(p, pw, clear) { calls = calls.concat(['save:' + p.name + ':' + p.engine + ':' + p.host + ':' + p.port + ':' + p.user + ':' + p.database + ':' + pw]); return '' }
    function removeProfile(id) {}
    function setWindowActive(a) {}
})";
static const char kFakeMail[] = R"(import QtQml
QtObject {
    property string state: 'ready'
    property string status: 'notmuch 0.39'
    property bool busy: false
    property var folders: [ { key: 'inbox', title: 'Boîte de réception', query: 'tag:inbox', count: 5 }, { key: 'unread', title: 'Non lus', query: 'tag:unread', count: 4 }, { key: 'all', title: 'Tous les messages', query: '*', count: 6 } ]
    property string folderKey: 'inbox'
    property string query: ''
    property var threads: [
        { id: '01', when: '10:00', matched: 1, total: 1, authors: 'Alice', subject: 'Rapport', tags: ['inbox', 'unread', 'flagged', 'attachment'], unread: true, flagged: true, attachment: true },
        { id: '02', when: '22 oct. 08:30', matched: 2, total: 2, authors: 'Zoë Müller, bob', subject: 'Café au lait', tags: ['inbox'], unread: false, flagged: false, attachment: false } ]
    property bool hasMore: false
    property string selectedThreadId: ''
    property var messages: []
    property bool threadBusy: false
    property string threadStatus: ''
    property var calls: []
    function setWindowActive(a) {}
    function refresh() { calls = calls.concat(['refresh']) }
    function openFolder(k) { calls = calls.concat(['folder:' + k]) }
    function search(t) { calls = calls.concat(['search:' + t]) }
    function loadMore() {}
    function openThread(id) { calls = calls.concat(['open:' + id]); selectedThreadId = id
        messages = [ { id: 'a', depth: 0, match: true, from: 'Alice <alice@example.org>', to: 'me@example.net', cc: '', date: 'Thu, 22 Oct 2026 08:30:00 +0000', when: '10:00', subject: 'Rapport', tags: ['inbox', 'unread'], unread: true,
            text: 'Voir le rapport.', html: '<p>Voir <a href=\"https://x.example\">le rapport</a>.</p>', htmlPlain: 'Voir le rapport.', hasText: true, hasHtml: true, htmlLinks: 1, htmlScripts: 0, htmlImages: 0,
            attachments: [ { name: 'Rapport été.pdf', type: 'application/pdf', size: 24, part: 3 } ] } ] }
    function closeThread() { selectedThreadId = ''; messages = [] }
    function setTag(id, tag, on) { calls = calls.concat(['tag:' + id + ':' + tag + ':' + on]) }
    signal sendChanged()
    signal actionChanged()
    property var accounts: [ { name: 'perso', from: 'me@example.net' } ]
    property string displayName: ''
    property string sendState: 'idle'
    property string sendStatus: ''
    property bool actionBusy: false
    property string actionStatus: ''
    property string actionOutput: ''
    property var setupState: ({})
    property var setupFiles: []
    function replyDraft(m) { return { to: m.from, subject: 'Re: ' + m.subject, body: '\n\n> ' + m.text, inReplyTo: m.id } }
    function resetSend() { sendState = 'idle'; sendStatus = '' }
    function send(d) { calls = calls.concat(['send:' + d.account + ':' + d.to + ':' + d.subject + ':' + d.inReplyTo]); sendState = 'sent'; sendStatus = 'Message envoyé'; sendChanged(); return '' }
    function probeSetup(n) { calls = calls.concat(['probe:' + n]); setupState = { ok: true, home: '/home/u', mbsync: 'mbsync 1.5.1', msmtp: 'msmtp 1.8', notmuch: 'notmuch 0.39', mailRoot: '/home/u/Mail' }; actionChanged() }
    function previewSetup(spec) { calls = calls.concat(['preview:' + spec.name + ':' + spec.imapSecurity + ':' + spec.smtpSecurity + ':' + spec.imapPort + ':' + spec.smtpPort])
        setupFiles = [ { key: 'mbsyncrc', path: '/home/u/.mbsyncrc', kind: 'block', shown: 'Pass \"********\"', content: 'Pass \"secret\"' },
                       { key: 'msmtprc', path: '/home/u/.msmtprc', kind: 'block', shown: 'password ********', content: 'password secret' } ]; return '' }
    function applySetup(keys) { calls = calls.concat(['apply:' + keys.join(',')]) }
    function clearSetup() { setupFiles = [] }
    function testSmtp(a) { calls = calls.concat(['smtp:' + a]) }
    function testImap(a) { calls = calls.concat(['imap:' + a]) }
    function syncNow(a) { calls = calls.concat(['sync:' + a]) }
    function cancelAction() {}
    property string attachStatus: ''
    function attachmentFileName(n) { return n }
    function saveAttachmentToServer(id, part, name) { calls = calls.concat(['attach-server:' + id + ':' + part + ':' + name]); attachStatus = 'Enregistré sur le serveur : /home/u/Downloads/' + name }
    function downloadAttachment(id, part, name, url) { calls = calls.concat(['attach-pc:' + id + ':' + part + ':' + name]) }
})";
static const char kFakeRemote[] = R"(import QtQml
QtObject { property bool connected: true; property string serverLabel: "fixture@server"; property bool linkLost: false; property string linkText: "Connexion perdue à 12:00 — les données affichées datent d’avant la coupure." })";
#endif

class ShellLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("CYBOU-tests"); QCoreApplication::setApplicationName("fortiq-layout");
        QTemporaryDir temp;QVERIFY(temp.isValid());QImage pixel(4,4,QImage::Format_ARGB32);pixel.fill(Qt::red);
        const auto path=temp.filePath("resource.png");QVERIFY(pixel.save(path));
        SafeMarkdownDocument doc;
        QVERIFY(!doc.resource(QTextDocument::ImageResource,QUrl::fromLocalFile(path)).isValid());
        QVERIFY(!doc.resource(QTextDocument::ImageResource,QUrl("https://example.invalid/image.png")).isValid());
        QVERIFY(!doc.resource(QTextDocument::ImageResource,QUrl("data:image/png;base64,AA==")).isValid());
    }
    void viewport_data() {
        QTest::addColumn<QSize>("size"); QTest::addColumn<QString>("preview");
        for (const QSize size : {QSize(1536,1024), QSize(1600,950), QSize(1024,768), QSize(768,1024), QSize(390,844)})
            for (const QString &preview : {QString(), QString("logs"), QString("editor"), QString("incident"), QString("service"), QString("servers"), QString("terminal"), QString("disks"), QString("docs")})
                QTest::newRow(qPrintable(QString("%1x%2-%3").arg(size.width()).arg(size.height()).arg(preview.isEmpty() ? "files" : preview))) << size << preview;
    }
    void viewport() {
        QFETCH(QSize, size); QFETCH(QString, preview);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("shellFixture", true);
        engine.rootContext()->setContextProperty("shellFixturePreview", preview);
        engine.rootContext()->setContextProperty("shellWallpaper", QUrl());
        engine.rootContext()->setContextProperty("shellWallpaperName", "");
        engine.rootContext()->setContextProperty("remote", QVariant::fromValue<QObject *>(nullptr));
        engine.rootContext()->setContextProperty("logs", QVariant::fromValue<QObject *>(nullptr));
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadFromModule("Fortiq.App", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(size); settle(window);
        auto *shell = window->findChild<QQuickItem *>("desktopShell"); QVERIFY(shell);
        if(preview=="docs") { shell->setProperty("documentsOpen",true);shell->setProperty("foregroundApp","docs"); }
        if(preview=="disks") { shell->setProperty("disksOpen",true);shell->setProperty("foregroundApp","disks"); }
        if(preview=="terminal") { shell->setProperty("terminalOpen",true);shell->setProperty("foregroundApp","terminal"); }
        fortiq::BackNavigationFilter back(window, [shell] {
            QVariant handled;
            return QMetaObject::invokeMethod(shell, "handleBack", Q_RETURN_ARG(QVariant, handled)) && handled.toBool();
        });
        auto *files = shell->findChild<QQuickItem *>("filesWindow"); QVERIFY(files);
        auto *logs = shell->findChild<QQuickItem *>("logsWindow"); QVERIFY(logs);
        auto *editor = files->findChild<QQuickItem *>("fileViewer"); QVERIFY(editor);
        const bool logApp = preview == "logs" || preview == "incident" || preview == "service";
        const bool adaptive = size.width() < 1280 || size.height() > size.width();
        QCOMPARE(shell->property("adaptive").toBool(), adaptive);
        QVERIFY(qAbs(shell->x()) < 0.01);
        QVERIFY(qAbs(shell->y()) < 0.01);
        QVERIFY(qAbs(shell->width() * shell->scale() - window->contentItem()->width()) < 0.01);
        QVERIFY(qAbs(shell->height() * shell->scale() - window->contentItem()->height()) < 0.01);
        if (adaptive) {
            QCOMPARE(shell->scale(), 1.0);
            for (auto *item : {files, logs}) {
                QVERIFY(item->x() >= 0 && item->y() >= 47);
                QVERIFY(item->x() + item->width() <= shell->width());
                QVERIFY(item->y() + item->height() <= shell->height() - 68);
            }
            QCOMPARE(files->isVisible(), !logApp && preview!="terminal" && preview!="disks" && preview!="docs");
            QCOMPARE(logs->isVisible(), logApp);
        } else {
            QCOMPARE(files->x(), 471.0); QCOMPARE(files->y(), 72.0);
            QCOMPARE(files->width(), 717.0); QCOMPARE(files->height(), 403.0);
        }
        if (preview == "servers") {
            auto *manager = shell->findChild<QQuickItem *>("serverManager"); QVERIFY(manager);
            shell->setProperty("serversOpen", true); manager->setProperty("editingId", "new");
            settle(window);
            // Window-level Back must work with focus in a form, also at master width.
            // The login screen cannot be dismissed: there is no session to go back to.
            QTest::keyRelease(window, Qt::Key_Back);
            QVERIFY(shell->property("serversOpen").toBool());
            QTest::keyRelease(window, Qt::Key_Back);
            QVERIFY(shell->property("serversOpen").toBool());
            manager->setProperty("editingId", "new");
        }
        const auto image = window->grabWindow(); QVERIFY(!image.isNull());
        QDir().mkpath("layout-captures");
        QVERIFY(image.save(QString("layout-captures/%1.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        if (!adaptive && preview.isEmpty()) {
            const QPoint start = files->mapToScene(QPointF(70, 350)).toPoint();
            const QPointF before(files->x(), files->y());
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, start + QPoint(25, 20), 30);
            QTest::mouseMove(window, start + QPoint(65, 45), 30);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(65, 45));
            QVERIFY(files->x() > before.x());
            QVERIFY(files->y() > before.y());
            files->setX(before.x()); files->setY(before.y());
            // Coalesced native gestures may deliver only press and release.
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(80, 50));
            QVERIFY(files->x() > before.x());
            QVERIFY(files->y() > before.y());
            files->setX(before.x()); files->setY(before.y());
            shell->setProperty("fixture", false);
            shell->setProperty("logsOpen", true);
            settle(window);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                files->mapToScene(QPointF(100, 20)).toPoint());
            QTRY_V(files->z() > logs->z());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                logs->mapToScene(QPointF(25, 80)).toPoint());
            QTRY_V(logs->z() > files->z());
            const auto logPosition=logs->position();
            const auto logStart=logs->mapToScene(QPointF(80,560)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,logStart);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,logStart+QPoint(60,40));
            QVERIFY(logs->x()>logPosition.x()); QVERIFY(logs->y()>logPosition.y());
            logs->setPosition(logPosition);
            QVERIFY(QMetaObject::invokeMethod(shell, "launch", Q_ARG(QVariant, QVariant("files"))));
            QTRY_V(files->z() > logs->z());
            // The passive activation handler must not consume child clicks.
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                files->mapToScene(QPointF(20, 23)).toPoint());
            QVERIFY(!shell->property("filesOpen").toBool());
            shell->setProperty("filesOpen", true);
            shell->setProperty("logsOpen", false);
            shell->setProperty("launcherOpen", true);
            settle(window);
            auto *launcher = shell->findChild<QQuickItem *>("launcherPanel"); QVERIFY(launcher);
            QVERIFY(launcher->x() >= 0);
            QVERIFY(launcher->y() + launcher->height() < shell->height() - 70);
            auto *appPanelItem = shell->findChild<QQuickItem *>("appPanel"); QVERIFY(appPanelItem);
            QCOMPARE(appPanelItem->x(), (shell->width() - appPanelItem->width()) / 2);   // bottom panel is centred
            QCOMPARE(launcher->x(), appPanelItem->x());                                  // the launcher opens above it
            shell->setProperty("controlOpen", true);
            settle(window);
            auto *control = shell->findChild<QQuickItem *>("controlCenter"); QVERIFY(control);
            QCOMPARE(control->x() + control->width(), shell->width() - 8);   // floating card against the right edge
            auto *switcher = shell->findChild<QQuickItem *>("backgroundSwitcher"); QVERIFY(switcher);
            QVERIFY(switcher->isVisible());
            QVERIFY(control->isAncestorOf(switcher));                    // the choice is part of the panel
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-control.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
            for (int index = 0; index < 3; ++index) {
                // Repeater delegates are visual children only: walk the item tree.
                std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&find](QQuickItem *root, const QString &name) -> QQuickItem * {
                    if (root->objectName() == name) return root;
                    for (QQuickItem *child : root->childItems()) if (auto *hit = find(child, name)) return hit;
                    return nullptr;
                };
                auto *choice = find(switcher, QString("backgroundChoice%1").arg(index)); QVERIFY(choice);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
                QTRY_C(shell->property("wallpaperName").toString(), (QStringList{"aurora", "desert", "city"}.at(index)));
            }
            QVERIFY(control->height() < 800);                           // fits its content, no empty tail
            // Presses inside the panel keep it open; a press elsewhere closes it and the launcher.
            shell->setProperty("launcherOpen", true);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene(QPointF(150, 300)).toPoint());
            QVERIFY(shell->property("controlOpen").toBool());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 700));
            QTRY_V(!shell->property("controlOpen").toBool());
            QTRY_V(!shell->property("launcherOpen").toBool());
            shell->setProperty("controlOpen", true);
            settle(window);
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-control-selected.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
            {   // Système: processes (filter, sort, details), services and the overview, all read-only.
                QQmlComponent sysComponent(&engine), sysRemoteComponent(&engine);
                sysComponent.setData(kFakeSystem, QUrl());
                sysRemoteComponent.setData(kFakeRemote, QUrl());
                auto *fakeSys = sysComponent.create(); QVERIFY2(fakeSys, qPrintable(sysComponent.errorString()));
                auto *sysRemote = sysRemoteComponent.create(); QVERIFY2(sysRemote, qPrintable(sysRemoteComponent.errorString()));
                fakeSys->setParent(window); sysRemote->setParent(window);
                shell->setProperty("systemController", QVariant::fromValue(fakeSys));
                shell->setProperty("systemOpen", true);
                shell->setProperty("foregroundApp", "system");
                settle(window);
                auto *sysWin = shell->findChild<QQuickItem *>("systemWindow"); QVERIFY(sysWin); QVERIFY(sysWin->isVisible());
                sysWin->setProperty("remote", QVariant::fromValue(sysRemote));
                QVERIFY(sysWin->x() >= 0 && sysWin->y() >= 47 && sysWin->x() + sysWin->width() <= shell->width());
                settle(window);
                auto *procList = findByName(sysWin, "systemProcessList"); QVERIFY(procList); QVERIFY(procList->isVisible());
                QCOMPARE(procList->property("count").toInt(), 2);                    // the kernel thread is hidden by default
                auto click = [&](QQuickItem *item) {
                    QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                click(findByName(sysWin, "systemKernel"));
                QCOMPARE(procList->property("count").toInt(), 3);
                click(findByName(sysWin, "systemKernel"));
                QCOMPARE(procList->property("count").toInt(), 2);
                // Default order is CPU descending: nginx first; PID ascending puts init first.
                QCOMPARE(sysWin->property("shownProcesses").toList().first().toMap().value("name").toString(), QString("nginx"));
                click(findByName(sysWin, "systemSort_pid"));                        // switches to PID, ascending
                QCOMPARE(sysWin->property("shownProcesses").toList().first().toMap().value("name").toString(), QString("init"));
                // Filtering by text and a row selection opens the details (read-only).
                auto *filter = findByName(sysWin, "systemFilter"); QVERIFY(filter);
                filter->forceActiveFocus();
                for (const char c : QByteArray("ngin")) QTest::keyClick(window, c);
                QTRY_C(procList->property("count").toInt(), 1);
                QVERIFY(QMetaObject::invokeMethod(procList, "forceLayout"));
                QQuickItem *row = nullptr;
                QVERIFY(QMetaObject::invokeMethod(procList, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)));
                click(row);
                auto *detail = findByName(sysWin, "systemDetail"); QVERIFY(detail);
                QTRY_V(detail->isVisible());
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-system.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                // Services: failed ones first, filter chips.
                click(findByName(sysWin, "systemSection_services"));
                QCOMPARE(sysWin->property("shownServices").toList().size(), 3);
                QCOMPARE(sysWin->property("shownServices").toList().first().toMap().value("unit").toString(), QString("broken.service"));
                click(findByName(sysWin, "systemServiceFilter_failed"));
                QCOMPARE(sysWin->property("shownServices").toList().size(), 1);
                click(findByName(sysWin, "systemServiceFilter_active"));
                QCOMPARE(sysWin->property("shownServices").toList().size(), 1);
                click(findByName(sysWin, "systemSection_overview"));
                settle(window);
                // The auto-refresh switch asks the controller for new data while the window is open.
                const int before = fakeSys->property("reloads").toInt();
                click(findByName(sysWin, "systemSection_processes"));
                click(findByName(sysWin, "systemAuto"));
                QTRY_VERIFY_WITH_TIMEOUT(fakeSys->property("reloads").toInt() > before, 8000);
                click(findByName(sysWin, "systemAuto"));
                {   // Link lost: a banner with the French sentence and "Réessayer"; it goes away when the link is back.
                    auto *banner = findByName(sysWin, "linkBanner"); QVERIFY(banner);
                    QVERIFY(!banner->isVisible());
                    sysRemote->setProperty("linkLost", true);
                    QTRY_V(banner->isVisible());
                    auto *bannerText = findByName(banner, "linkBannerText"); QVERIFY(bannerText);
                    QVERIFY(bannerText->property("text").toString().contains("Connexion perdue"));
                    QVERIFY(banner->x() >= 0 && banner->x() + banner->width() <= sysWin->width() && banner->y() >= 0 && banner->y() + banner->height() <= sysWin->height());
                    const int reloadsBefore = fakeSys->property("reloads").toInt();
                    click(findByName(banner, "linkBannerRetry"));
                    QTRY_V(fakeSys->property("reloads").toInt() > reloadsBefore);
                    QVERIFY(window->grabWindow().save(QString("layout-captures/%1-link-lost.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                    sysRemote->setProperty("linkLost", false);
                    QTRY_V(!banner->isVisible());
                }
                shell->setProperty("systemOpen", false);
                shell->setProperty("systemController", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // Courrier: folders, thread list, reader, tag actions.
                QQmlComponent mailComponent(&engine), mailRemoteComponent(&engine);
                mailComponent.setData(kFakeMail, QUrl());
                mailRemoteComponent.setData(kFakeRemote, QUrl());
                auto *fakeMail = mailComponent.create(); QVERIFY2(fakeMail, qPrintable(mailComponent.errorString()));
                auto *mailRemote = mailRemoteComponent.create(); QVERIFY2(mailRemote, qPrintable(mailRemoteComponent.errorString()));
                fakeMail->setParent(window); mailRemote->setParent(window);
                shell->setProperty("mailController", QVariant::fromValue(fakeMail));
                shell->setProperty("mailOpen", true);
                shell->setProperty("foregroundApp", "mail");
                settle(window);
                auto *mailWin = shell->findChild<QQuickItem *>("mailWindow"); QVERIFY(mailWin); QVERIFY(mailWin->isVisible());
                mailWin->setProperty("remote", QVariant::fromValue(mailRemote));
                QVERIFY(mailWin->x() >= 0 && mailWin->y() >= 47 && mailWin->x() + mailWin->width() <= shell->width());
                settle(window);
                auto click = [&](QQuickItem *item) {
                    QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                auto *list = findByName(mailWin, "mailThreadList"); QVERIFY(list); QVERIFY(list->isVisible());
                QCOMPARE(list->property("count").toInt(), 2);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                // Search runs the query typed in the box.
                auto *search = findByName(mailWin, "mailSearch"); QVERIFY(search);
                search->forceActiveFocus();
                for (const char c : QByteArray("from:alice")) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_V(fakeMail->property("calls").toStringList().contains("search:from:alice"));
                // A thread opens in the reader; tag buttons ask the controller.
                click(findByName(mailWin, "mailThread_01"));
                QTRY_V(fakeMail->property("calls").toStringList().contains("open:01"));
                auto *reader = findByName(mailWin, "mailReader"); QVERIFY(reader); QTRY_V(reader->isVisible());
                auto *messages = findByName(mailWin, "mailMessages"); QVERIFY(messages);
                QTRY_C(messages->property("count").toInt(), 1);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail-reader.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                // Attachments stay on the server: one button saves there, the other asks where to put it on this computer.
                click(findByName(mailWin, "mailAttachServer_0"));
                QCOMPARE(fakeMail->property("calls").toStringList().filter("attach-server:a:3:Rapport").size(), 1);
                auto *attachStatus = findByName(mailWin, "mailAttachStatus"); QVERIFY(attachStatus); QTRY_V(attachStatus->isVisible());
                QVERIFY(findByName(mailWin, "mailAttachPc_0")->isVisible());
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail-attachment.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                click(findByName(mailWin, "mailTag_unread"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("tag:01:unread:false"));      // the thread is unread: the button marks it read
                click(findByName(mailWin, "mailTag_inbox"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("tag:01:inbox:false"));       // archive
                click(findByName(mailWin, "mailFolder_unread"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("folder:unread"));
                // Reply opens the composer prefilled; sending goes through the controller and closes it.
                click(findByName(mailWin, "mailReply"));
                auto *compose = findByName(mailWin, "mailCompose"); QVERIFY(compose); QTRY_V(compose->isVisible());
                QCOMPARE(findByName(mailWin, "mailComposeSubject")->property("text").toString(), QString("Re: Rapport"));
                QCOMPARE(findByName(mailWin, "mailComposeTo")->property("text").toString(), QString("Alice <alice@example.org>"));
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail-compose.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                findByName(mailWin, "mailComposeTo")->setProperty("text", "bob@example.org");
                click(findByName(mailWin, "mailComposeSend"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("send:perso:bob@example.org:Re: Rapport:a"));
                QTRY_V(!compose->isVisible());
                QVERIFY(findByName(mailWin, "mailSendBanner")->isVisible());
                // The account wizard: nothing is written before the exact text is shown and the consent box is ticked.
                fakeMail->setProperty("sendState", "idle");
                click(findByName(mailWin, "mailAccounts"));
                auto *wizard = findByName(mailWin, "mailWizard"); QVERIFY(wizard); QTRY_V(wizard->isVisible());
                QVERIFY(fakeMail->property("calls").toStringList().contains("probe:perso"));
                findByName(mailWin, "mailWizEmail")->setProperty("text", "me@example.net");
                findByName(mailWin, "mailWizImapHost")->setProperty("text", "imap.example.net");
                findByName(mailWin, "mailWizImapUser")->setProperty("text", "me@example.net");
                findByName(mailWin, "mailWizSmtpHost")->setProperty("text", "smtp.example.net");
                findByName(mailWin, "mailWizPassword")->setProperty("text", "secret");
                click(findByName(mailWin, "mailWizPreview"));
                QTRY_V(fakeMail->property("calls").toStringList().contains("preview:perso:tls:starttls:993:587"));
                auto *files = findByName(mailWin, "mailWizFiles"); QVERIFY(files); QTRY_V(files->isVisible());
                auto *shown = findByName(mailWin, "mailWizText_msmtprc"); QVERIFY(shown);
                QCOMPARE(shown->property("text").toString(), QString("password ********"));       // masked by default
                auto *write = findByName(mailWin, "mailWizWrite"); QVERIFY(write);
                QVERIFY(!write->property("enabled").toBool());                                      // no consent yet
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail-wizard.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                click(findByName(mailWin, "mailWizReveal"));
                QCOMPARE(shown->property("text").toString(), QString("password secret"));          // exact text on request
                auto *wizardFlick = findByName(mailWin, "mailWizFlick"); QVERIFY(wizardFlick);
                wizardFlick->setProperty("contentY", wizardFlick->property("contentHeight").toDouble() - wizardFlick->height());   // the consent box sits below the files
                settle(window);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mail-wizard-bottom.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                click(findByName(mailWin, "mailWizConsent"));
                QTRY_V(write->property("enabled").toBool());
                click(write);
                QTRY_V(fakeMail->property("calls").toStringList().contains("apply:mbsyncrc,msmtprc"));
                QVERIFY(findByName(mailWin, "mailWizPassword")->property("text").toString().isEmpty());   // the password field is cleared once submitted
                click(findByName(mailWin, "mailWizTestSmtp"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("smtp:perso"));
                click(findByName(mailWin, "mailWizSync"));
                QVERIFY(fakeMail->property("calls").toStringList().contains("sync:perso"));
                shell->setProperty("mailOpen", false);
                shell->setProperty("mailController", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // Bases de données: profile list, schema, read-only editor and the profile form.
                QQmlComponent dbComponent(&engine), dbRemoteComponent(&engine);
                dbComponent.setData(kFakeDb, QUrl());
                dbRemoteComponent.setData(kFakeRemote, QUrl());
                auto *fakeDb = dbComponent.create(); QVERIFY2(fakeDb, qPrintable(dbComponent.errorString()));
                auto *dbRemote = dbRemoteComponent.create(); QVERIFY2(dbRemote, qPrintable(dbRemoteComponent.errorString()));
                fakeDb->setParent(window); dbRemote->setParent(window);
                shell->setProperty("dbController", QVariant::fromValue(fakeDb));
                shell->setProperty("dbOpen", true);
                shell->setProperty("foregroundApp", "db");
                settle(window);
                auto *dbWin = shell->findChild<QQuickItem *>("dbWindow"); QVERIFY(dbWin); QVERIFY(dbWin->isVisible());
                dbWin->setProperty("remote", QVariant::fromValue(dbRemote));
                QVERIFY(dbWin->x() >= 0 && dbWin->y() >= 47 && dbWin->x() + dbWin->width() <= shell->width());
                settle(window);
                auto click = [&](QQuickItem *item) {
                    QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                auto *rows = findByName(dbWin, "dbRows"); QVERIFY(rows); QVERIFY(rows->isVisible());
                QCOMPARE(rows->property("count").toInt(), 2);
                QCOMPARE(dbWin->property("schemaRows").toList().size(), 1);          // collapsed: only the table
                QVERIFY(findByName(dbWin, "dbInfo")); QVERIFY(!findByName(dbWin, "dbError")->isVisible());
                {   // Copier: the result as tab-separated text, NULL spelled out.
                    auto *copy = findByName(dbWin, "dbCopyResult"); QVERIFY(copy); QTRY_V(copy->isVisible());
                    QVERIFY(copy->x() + copy->width() <= dbWin->width());
                    QGuiApplication::clipboard()->clear();
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, copy->mapToScene(QPointF(copy->width() / 2, copy->height() / 2)).toPoint());
                    QTRY_V(QGuiApplication::clipboard()->text().startsWith("id\tname\tscore\n"));
                }
                // The connected role: a banner says what it may do; details and a copyable read-only role recipe open on a click.
                auto *role = findByName(dbWin, "dbRoleBanner"); QVERIFY(role); QTRY_V(role->isVisible());
                QCOMPARE(role->height(), 30.0);
                auto *recipe = findByName(dbWin, "dbRoleRecipe"); QVERIFY(recipe); QVERIFY(!recipe->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, role->mapToScene(QPointF(role->width() / 2, 12)).toPoint());
                QTRY_V(recipe->isVisible());
                QTRY_V(role->height() > 60);
                QVERIFY(recipe->property("text").toString().contains("CREATE ROLE fortiq_ro"));
                QVERIFY(findByName(dbWin, "dbRoleCopy")->isVisible());
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-db-role.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, role->mapToScene(QPointF(role->width() / 2, 12)).toPoint());
                QTRY_V(!recipe->isVisible());
                // A role without write rights gets a quiet confirmation; an unchecked one shows nothing alarming.
                fakeDb->setProperty("roleCheck", QVariantMap { { "ran", true }, { "level", "ok" }, { "summary", "Ce role na aucun droit decriture visible." }, { "findings", QStringList() }, { "recommendation", "" } });
                QTRY_C(role->height(), 30.0);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, role->mapToScene(QPointF(role->width() / 2, 12)).toPoint());
                QVERIFY(!recipe->isVisible());                                                          // nothing to expand
                fakeDb->setProperty("roleCheck", QVariantMap());
                QTRY_V(!role->isVisible());
                fakeDb->setProperty("roleCheck", QVariantMap { { "ran", true }, { "level", "warning" }, { "summary", "restore" }, { "findings", QStringList() }, { "recommendation", "" } });
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-db.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                // Ctrl+Enter runs the SQL in the editor; the SQL goes to the controller as typed.
                auto *editor = findByName(dbWin, "dbEditor"); QVERIFY(editor);
                editor->setProperty("text", "select 1");
                editor->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
                QTRY_V(fakeDb->property("calls").toStringList().contains("run:select 1"));
                // The profile form collects a PostgreSQL profile; the password goes to the controller once and the field is cleared.
                click(findByName(dbWin, "dbNewProfile"));
                auto *form = findByName(dbWin, "dbForm"); QVERIFY(form); QTRY_V(form->isVisible());
                click(findByName(dbWin, "dbEngine_postgres"));
                findByName(dbWin, "dbName")->setProperty("text", "Prod");
                findByName(dbWin, "dbHost")->setProperty("text", "db.local");
                findByName(dbWin, "dbPort")->setProperty("text", "5433");
                findByName(dbWin, "dbUser")->setProperty("text", "ro");
                findByName(dbWin, "dbDatabase")->setProperty("text", "app");
                findByName(dbWin, "dbPassword")->setProperty("text", "s3cret");
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-db-form.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                click(findByName(dbWin, "dbFormSave"));
                QTRY_V(!form->isVisible());
                QVERIFY(fakeDb->property("calls").toStringList().contains("save:Prod:postgres:db.local:5433:ro:app:s3cret"));
                QVERIFY(findByName(dbWin, "dbPassword")->property("text").toString().isEmpty());
                shell->setProperty("dbOpen", false);
                shell->setProperty("dbController", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // Réseau: read-only sections and diagnostics that run on the server.
                QQmlComponent netComponent(&engine), netRemoteComponent(&engine);
                netComponent.setData(kFakeNetwork, QUrl());
                netRemoteComponent.setData(kFakeRemote, QUrl());
                auto *fakeNet = netComponent.create(); QVERIFY2(fakeNet, qPrintable(netComponent.errorString()));
                auto *netRemote = netRemoteComponent.create(); QVERIFY2(netRemote, qPrintable(netRemoteComponent.errorString()));
                fakeNet->setParent(window); netRemote->setParent(window);
                shell->setProperty("networkController", QVariant::fromValue(fakeNet));
                shell->setProperty("networkOpen", true);
                shell->setProperty("foregroundApp", "network");
                settle(window);
                auto *netWin = shell->findChild<QQuickItem *>("networkWindow"); QVERIFY(netWin); QVERIFY(netWin->isVisible());
                netWin->setProperty("remote", QVariant::fromValue(netRemote));
                QVERIFY(netWin->x() >= 0 && netWin->y() >= 47 && netWin->x() + netWin->width() <= shell->width());
                settle(window);
                auto *netList = findByName(netWin, "networkList"); QVERIFY(netList); QVERIFY(netList->isVisible());
                QCOMPARE(netList->property("count").toInt(), 2);                    // lo and eth0
                auto clickSection = [&](const char *key) {
                    auto *item = findByName(netWin, QString("networkSection_%1").arg(key)); QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                clickSection("ports");
                QCOMPARE(netList->property("count").toInt(), 2);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-network.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                clickSection("routes");
                QCOMPARE(netList->property("count").toInt(), 1);
                clickSection("dns");
                QVERIFY(!netList->isVisible());
                clickSection("diag");
                auto *target = findByName(netWin, "networkTarget"); QVERIFY(target);
                target->forceActiveFocus();
                for (const char c : QByteArray("example.org")) QTest::keyClick(window, c);
                auto *ping = findByName(netWin, "networkProbe_ping"); QVERIFY(ping);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, ping->mapToScene(QPointF(ping->width() / 2, ping->height() / 2)).toPoint());
                QTRY_C(fakeNet->property("calls").toStringList(), QStringList{ "probe:ping:example.org" });
                auto *output = findByName(netWin, "networkProbeOutput"); QVERIFY(output);
                QTRY_V(output->property("text").toString().contains("4 packets transmitted"));
                {   // Copier: the diagnostic and its target reach the clipboard.
                    auto *copy = findByName(netWin, "networkCopyProbe"); QVERIFY(copy); QTRY_V(copy->isVisible());
                    QVERIFY(copy->x() + copy->width() <= netWin->width());
                    QGuiApplication::clipboard()->clear();
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, copy->mapToScene(QPointF(copy->width() / 2, copy->height() / 2)).toPoint());
                    QTRY_V(QGuiApplication::clipboard()->text().contains("ping example.org") && QGuiApplication::clipboard()->text().contains("4 packets transmitted"));
                }
                shell->setProperty("networkOpen", false);
                shell->setProperty("networkController", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // Navigateur: a window over curl on the server; links, address bar and details go through the controller.
                QQmlComponent browserComponent(&engine), remoteComponent(&engine);
                browserComponent.setData(kFakeBrowser, QUrl());
                remoteComponent.setData(kFakeRemote, QUrl());
                auto *fakeBrowser = browserComponent.create(); QVERIFY2(fakeBrowser, qPrintable(browserComponent.errorString()));
                auto *fakeRemote = remoteComponent.create(); QVERIFY2(fakeRemote, qPrintable(remoteComponent.errorString()));
                fakeBrowser->setParent(window); fakeRemote->setParent(window);
                shell->setProperty("browserController", QVariant::fromValue(fakeBrowser));
                shell->setProperty("webOpen", true);
                shell->setProperty("foregroundApp", "web");
                settle(window);
                auto *web = shell->findChild<QQuickItem *>("webWindow"); QVERIFY(web); QVERIFY(web->isVisible());
                web->setProperty("remote", QVariant::fromValue(fakeRemote));
                QVERIFY(web->x() >= 0 && web->y() >= 47 && web->x() + web->width() <= shell->width());
                settle(window);
                auto *html = findByName(web, "webHtml"); QVERIFY(html); QVERIFY(html->isVisible());
                QVERIFY(html->property("text").toString().contains("Hello page"));
                QVERIFY(!html->property("text").toString().contains("<script"));
                // Typing an address sends exactly that text to the controller.
                auto *address = findByName(web, "webAddress"); QVERIFY(address);
                address->forceActiveFocus();
                address->setProperty("text", QString());
                for (const char c : QByteArray("example.org/a")) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_C(fakeBrowser->property("calls").toStringList(), QStringList{ "open:example.org/a" });
                // A link clicked in the page is handed over unresolved (the controller applies the rules).
                QVERIFY(QMetaObject::invokeMethod(html, "linkActivated", Q_ARG(QString, QString("https://example.com/more"))));
                QTRY_C(fakeBrowser->property("calls").toStringList(), (QStringList{ "open:example.org/a", "link:https://example.com/more" }));
                // Response details open beside the page and list the headers.
                auto *infoButton = findByName(web, "webInfo"); QVERIFY(infoButton);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, infoButton->mapToScene(QPointF(infoButton->width() / 2, infoButton->height() / 2)).toPoint());
                auto *infoPanel = findByName(web, "webInfoPanel"); QVERIFY(infoPanel);
                QTRY_V(infoPanel->isVisible());
                settle(window);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-web.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                // An error page is explicit and never shows stale content.
                fakeBrowser->setProperty("kind", "error");
                fakeBrowser->setProperty("state", "error");
                fakeBrowser->setProperty("status", "Nom d'hôte introuvable depuis le serveur (DNS).");
                settle(window);
                QVERIFY(!html->isVisible());
                // An internal address is asked about before anything is fetched: two answers, nothing else is reachable meanwhile.
                auto *confirm = findByName(web, "webConfirm"); QVERIFY(confirm); QVERIFY(!confirm->isVisible());
                fakeBrowser->setProperty("pendingConfirm", QVariantMap { { "url", "http://127.0.0.1:8080/admin" }, { "host", "127.0.0.1" }, { "port", 8080 }, { "hop", 1 }, { "from", "https://example.com/go" },
                    { "reason", "Cette adresse est interne au serveur : 127.0.0.1 (boucle locale (localhost))." },
                    { "details", QVariantList { QVariantMap { { "ip", "127.0.0.1" }, { "label", "boucle locale (localhost)" }, { "cls", "internal" } } } } });
                fakeBrowser->setProperty("state", "confirm");
                QTRY_V(confirm->isVisible());
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-web-confirm.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                auto clickItem = [&](QQuickItem *item) {
                    QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                clickItem(findByName(web, "webConfirmCancel"));
                QTRY_V(fakeBrowser->property("calls").toStringList().contains("cancel-confirm"));
                QVERIFY(!confirm->isVisible());
                fakeBrowser->setProperty("pendingConfirm", QVariantMap { { "url", "http://127.0.0.1:8080/admin" }, { "host", "127.0.0.1" }, { "port", 8080 }, { "hop", 0 }, { "from", "" }, { "reason", "interne" }, { "details", QVariantList() } });
                fakeBrowser->setProperty("state", "confirm");
                QTRY_V(confirm->isVisible());
                clickItem(findByName(web, "webConfirmOpen"));
                QTRY_V(fakeBrowser->property("calls").toStringList().contains("confirm"));
                QVERIFY(!confirm->isVisible());
                // Back closes the question as a refusal.
                fakeBrowser->setProperty("state", "confirm");
                QTRY_V(confirm->isVisible());
                QVariant handled;
                QVERIFY(QMetaObject::invokeMethod(web, "handleBack", Q_RETURN_ARG(QVariant, handled)));
                QVERIFY(handled.toBool());
                QCOMPARE(fakeBrowser->property("calls").toStringList().count("cancel-confirm"), 2);
                shell->setProperty("webOpen", false);
                shell->setProperty("browserController", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // The shared task panel lists the tasks of every app and cancels through the manager.
                fortiq::TaskManager manager;
                shell->setProperty("taskManager", QVariant::fromValue<QObject *>(&manager));
                fortiq::TaskManager::Spec spec;
                spec.kind = "upload"; spec.title = "a.txt"; spec.destination = "/home"; spec.generation = 1;
                const int id = manager.create(spec);
                manager.start(id);
                manager.setProgress(id, 10, 100);
                settle(window);
                auto *panel = shell->findChild<QQuickItem *>("taskPanel"); QVERIFY(panel);
                QVERIFY(panel->isVisible());
                QVERIFY(panel->x() >= 0 && panel->x() + panel->width() <= shell->width());
                QVERIFY(panel->y() >= 0 && panel->y() + panel->height() <= shell->height());
                auto *cancel = findByName(panel, "taskCancel"); QVERIFY(cancel); QVERIFY(cancel->isVisible());
                // the click is repeated until it has an effect: the first one can arrive before the panel has taken its place on a busy machine
                QTRY_V([&] {
                    if (manager.state(id).value() == fortiq::TaskManager::State::Running)
                        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
                    return manager.state(id).value() == fortiq::TaskManager::State::Cancelling;   // a request, not a stop
                }());
                manager.markCancelled(id, "stopped");
                auto *clear = findByName(panel, "taskClear");
                QTRY_V(clear && clear->isVisible()); settle(window);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, clear->mapToScene(QPointF(clear->width() / 2, clear->height() / 2)).toPoint());
                QTRY_C(manager.count(), 0);
                QTRY_V(!panel->isVisible());
                shell->setProperty("taskManager", QVariant::fromValue<QObject *>(nullptr));
            }
            {   // Login screen: connecting works like logging in to Linux, and logout returns to it.
                QQmlComponent fakeComponent(&engine);
                fakeComponent.setData(kFakeController, QUrl());
                auto *fake = fakeComponent.create(); QVERIFY2(fake, qPrintable(fakeComponent.errorString())); fake->setParent(window);
                shell->setProperty("serversOpen", true);
                settle(window);
                auto *login = shell->findChild<QQuickItem *>("serverManager"); QVERIFY(login);
                login->setProperty("controller", QVariant::fromValue(fake));   // only the login screen sees the fake
                settle(window);
                QVERIFY(login->isVisible());
                QCOMPARE(login->width(), shell->width()); QCOMPARE(login->height(), shell->height());   // replaces the desktop
                auto *secretInput = findByName(login, "loginSecret"); QVERIFY(secretInput); QVERIFY(secretInput->isVisible());
                settle(window);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-login.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                secretInput->forceActiveFocus();
                for (const char c : {'p', 'w', '1'}) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_C(fake->property("calls").toStringList(), QStringList{"a:pw1"});
                // Clicking a key account connects at once.
                auto *keyTile = findByName(login, "loginTile_b"); QVERIFY(keyTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, keyTile->mapToScene(QPointF(keyTile->width() / 2, 50)).toPoint());
                QTRY_C(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:"}));
                // A remembered secret connects at once through the protected store; no typing needed.
                auto *savedTile = findByName(login, "loginTile_c"); QVERIFY(savedTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, savedTile->mapToScene(QPointF(savedTile->width() / 2, 50)).toPoint());
                QTRY_C(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:", "saved:c"}));
                QVERIFY(findByName(login, "loginForget")->isVisible());
                // "Se souvenir" sends the typed secret to the controller with remember = true.
                auto *plainTile = findByName(login, "loginTile_a"); QVERIFY(plainTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, plainTile->mapToScene(QPointF(plainTile->width() / 2, 50)).toPoint());
                auto *box = findByName(login, "loginRemember"); QVERIFY(box); QVERIFY(box->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint());
                secretInput->forceActiveFocus();
                for (const char c : {'p', 'w', '2'}) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_C(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:", "saved:c", "remember:a:pw2"}));
                // Logout from the session menu closes the windows and returns to the login screen.
                login->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
                shell->setProperty("serversOpen", false);
                shell->setProperty("filesOpen", true);
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-session.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                auto *pill = findByName(shell, "sessionPill"); QVERIFY(pill); QVERIFY(pill->isVisible());
                QVERIFY(pill->x() + pill->width() > shell->width() - 20);                  // at the right, where the avatar was
                shell->setProperty("sessionMenuOpen", false);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pill->mapToScene(QPointF(pill->width() / 2, pill->height() / 2)).toPoint());
                QTRY_V(shell->property("serversOpen").toBool());   // not connected: the pill leads to the login screen
                shell->setProperty("serversOpen", false);
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                auto *logout = findByName(shell, "logoutButton"); QVERIFY(logout); QVERIFY(logout->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, logout->mapToScene(QPointF(logout->width() / 2, logout->height() / 2)).toPoint());
                QTRY_V(shell->property("serversOpen").toBool());
                QVERIFY(!shell->property("filesOpen").toBool());
                QVERIFY(!shell->property("sessionMenuOpen").toBool());
                shell->setProperty("serversOpen", false);
            }
            {   // Ending the session asks first while a task that guards it is running (audit backlog 1).
                fortiq::TaskManager manager;
                shell->setProperty("taskManager", QVariant::fromValue<QObject *>(&manager));
                auto clickItem = [&](QQuickItem *item) {
                    QVERIFY(item);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    settle(window);
                };
                fortiq::TaskManager::Spec save;
                save.kind = "docSave"; save.title = "notes.md"; save.source = "user@host"; save.cancellable = false;   // a write: guards by default
                const int saveId = manager.begin(save);
                shell->setProperty("filesOpen", true);
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                auto *guard = findByName(shell, "sessionGuard"); QVERIFY(guard); QVERIFY(!guard->isVisible());
                clickItem(findByName(shell, "logoutButton"));
                QTRY_V(guard->isVisible());                                        // asked, nothing ended yet
                QVERIFY(!shell->property("serversOpen").toBool());
                QVERIFY(shell->property("filesOpen").toBool());
                QCOMPARE(guard->property("tasks").toList().size(), 1);                  // exactly the one write is listed
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-session-guard.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                clickItem(findByName(shell, "sessionGuardStay"));                      // "Attendre": everything stays as it was
                QVERIFY(!guard->isVisible());
                QVERIFY(!shell->property("serversOpen").toBool());
                QVERIFY(shell->property("filesOpen").toBool());
                QCOMPARE(manager.state(saveId).value(), fortiq::TaskManager::State::Running);
                // The Back key closes the question too.
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                clickItem(findByName(shell, "logoutButton"));
                QTRY_V(guard->isVisible());
                QVariant handled;
                QVERIFY(QMetaObject::invokeMethod(shell, "handleBack", Q_RETURN_ARG(QVariant, handled)));
                QVERIFY(handled.toBool()); QVERIFY(!guard->isVisible());
                // Quitting asks as well (the window close handler uses allowAppClose).
                QVariant allowed;
                QVERIFY(QMetaObject::invokeMethod(shell, "allowAppClose", Q_RETURN_ARG(QVariant, allowed)));
                QVERIFY(!allowed.toBool()); QVERIFY(guard->isVisible());
                clickItem(findByName(shell, "sessionGuardStay"));
                // "Se déconnecter quand même": the session ends, the task is not touched by the dialog.
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                clickItem(findByName(shell, "logoutButton"));
                QTRY_V(guard->isVisible());
                clickItem(findByName(shell, "sessionGuardGo"));
                QTRY_V(shell->property("serversOpen").toBool());
                QVERIFY(!shell->property("filesOpen").toBool());
                QVERIFY(!guard->isVisible());
                QCOMPARE(manager.state(saveId).value(), fortiq::TaskManager::State::Running);   // settling is the controllers' job (disconnect)
                QVERIFY(manager.succeed(saveId, "done"));
                // Reads, views and the live follow do not ask.
                fortiq::TaskManager::Spec read;
                read.kind = "logsLoad"; read.title = "Lecture"; read.guards = false; read.session = true; read.transient = true;
                const int readId = manager.begin(read);
                shell->setProperty("serversOpen", false);
                shell->setProperty("filesOpen", true);
                shell->setProperty("sessionMenuOpen", true);
                settle(window);
                clickItem(findByName(shell, "logoutButton"));
                QTRY_V(shell->property("serversOpen").toBool());
                QVERIFY(!guard->isVisible());
                QVERIFY(manager.succeed(readId, ""));
                shell->setProperty("serversOpen", false);
                shell->setProperty("taskManager", QVariant::fromValue<QObject *>(nullptr));
            }
            shell->setProperty("fixture", true);
        }
        if (preview == "incident" && adaptive && size.width() < 700) {
            auto *title = logs->findChild<QQuickItem *>("incidentTitle"); QVERIFY(title);
            auto *controls = logs->findChild<QQuickItem *>("incidentControls"); QVERIFY(controls);
            QVERIFY(title->y() + title->height() <= controls->y());
        }
        if(preview=="docs") {
            auto *doc=shell->findChild<QQuickItem *>("documentsWindow");QVERIFY(doc);QVERIFY(doc->isVisible());
            QVERIFY(doc->x()>=0 && doc->y()>=47);QVERIFY(doc->x()+doc->width()<=shell->width());
            auto *button=doc->findChild<QQuickItem *>("documentsConnectButton");QVERIFY(button);QVERIFY(button->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
            QTRY_V(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
            QQmlComponent component(&engine);
#ifndef Q_MOC_RUN // moc cannot parse this raw string
            component.setData(R"(import QtQml
                QtObject {
                    property bool connected:true
                    property string currentDirectory:"/home/fixture"
                    property string serverLabel:"fixture"
                    property bool busy:false
                    property bool hasDocument:true
                    property bool isNew:false
                    property bool canSave:true
                    property string path:"/home/fixture/notes.md"
                    property string base:"# Title\n**bold** and [link](https:\u002f\u002fexample.invalid)\n"
                    property string text:base
                    readonly property bool dirty:text!==base
                    property string sourceLabel:"fixture"
                    property string status:"UTF-8"
                    signal loaded()
                    signal saved(string path)
                    function discardDraft(){text=base;loaded()}
                    function save(){base=text;saved(path)}
                    function saveAs(p){save()}
                    function open(p){}
                    function createNew(){}
                    function copyDraft(){}
                })",QUrl());
#endif
            auto *fixture=component.create();QVERIFY2(fixture,qPrintable(component.errorString()));fixture->setParent(window);
            shell->setProperty("documentsController",QVariant::fromValue(fixture));doc->setProperty("remote",QVariant::fromValue(fixture));
            auto *editor=doc->findChild<QQuickItem *>("documentsEditor");QVERIFY(editor);
            auto *render=doc->findChild<QQuickItem *>("documentsPreview");QVERIFY(render);
            QString rendered;
            QTRY_V([&] { rendered.clear(); return QMetaObject::invokeMethod(render,"plainText",Q_RETURN_ARG(QString,rendered)) && rendered.contains("Title") && rendered.contains("bold") && !rendered.contains("**bold**"); }());   // the preview renders asynchronously
            render->setProperty("source",QString(64001,'x'));QVERIFY(render->property("limited").toBool());
            editor->setProperty("text",QString::fromUtf8("# Modifié\nПривет **texte**\n"));QVERIFY(fixture->property("dirty").toBool());
            editor->forceActiveFocus();QTest::keyClick(window,Qt::Key_S,Qt::ControlModifier);QVERIFY(!fixture->property("dirty").toBool());
            editor->setProperty("text",QString("unsaved"));QVERIFY(fixture->property("dirty").toBool());
            QVERIFY(QMetaObject::invokeMethod(doc,"request",Q_ARG(QVariant,QVariant("close")),Q_ARG(QVariant,QVariant(""))));
            auto *dialog=doc->findChild<QQuickItem *>("documentsUnsavedDialog");QVERIFY(dialog);QVERIFY(dialog->isVisible());
            QTest::keyClick(window,Qt::Key_Escape);QVERIFY(!dialog->isVisible());QVERIFY(doc->isVisible());
            QCOMPARE(shell->property("foregroundApp").toString(),QString("docs"));
            QVariant allowed;QVERIFY(QMetaObject::invokeMethod(shell,"allowAppClose",Q_RETURN_ARG(QVariant,allowed)));QVERIFY(!allowed.toBool());QVERIFY(dialog->isVisible());
            QTest::keyClick(window,Qt::Key_Escape);QVERIFY(!dialog->isVisible());
            editor->setProperty("text",QString::fromUtf8("# Projet Été\n\n**Objectif** : Bonjour Привет.\n\n- Document serveur\n- Enregistrement sûr\n\n> Note de travail\n"));
            fixture->setProperty("base",fixture->property("text"));
            if(adaptive)doc->setProperty("previewMode",true);
            settle(window);
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mdok.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
        if(preview=="disks") {
            auto *disk=shell->findChild<QQuickItem *>("disksWindow");QVERIFY(disk);QVERIFY(disk->isVisible());
            QVERIFY(disk->x()>=0 && disk->y()>=47);QVERIFY(disk->x()+disk->width()<=shell->width());
            auto *sidebar=disk->findChild<QQuickItem *>("disksSidebar");QVERIFY(sidebar);
            QCOMPARE(sidebar->isVisible(),!disk->property("drawer").toBool());
            auto *button=disk->findChild<QQuickItem *>("disksConnectButton");QVERIFY(button);QVERIFY(button->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
            QTRY_V(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
            if(disk->property("drawer").toBool()) {
                disk->setProperty("navigationOpen",true);QVERIFY(sidebar->isVisible());
                QTest::keyRelease(window,Qt::Key_Back);QVERIFY(!disk->property("navigationOpen").toBool());
                QCOMPARE(shell->property("foregroundApp").toString(),QString("disks"));
            }
            QQmlComponent component(&engine);
            component.setData(R"(import QtQml
                QtObject {
                    property bool connected:true
                    property string serverLabel:"fixture"
                    property bool busy:false
                    property string status:"Lecture seule"
                    property string versions:"util-linux fixture"
                    property string updated:"12:00:00"
                    property string deviceError:""
                    property string mountError:""
                    property var mounts:[]
                    property var devices:[]
                    function setWindowActive(active) {}
                    function reload() {}
                })",QUrl());
            auto *fixture=component.create();QVERIFY2(fixture,qPrintable(component.errorString()));fixture->setParent(window);
            const QString mountPath=QString::fromUtf8("/media/Été espace");
            fixture->setProperty("mounts",QVariantList{QVariantMap{{"target",mountPath},{"source","/dev/sda1"},{"fstype","ext4"},{"sizeText","1.0 Gio"},{"usedText","0.5 Gio"},{"availText","0.4 Gio"},{"fraction",0.5}}});
            fixture->setProperty("devices",QVariantList{QVariantMap{{"path","/dev/sda1"},{"type","part"},{"fstype","ext4"},{"label","<b>plain text</b>"},{"sizeText","1.0 Gio"},{"readOnlyKnown",true},{"readOnly",false},{"pkname","sda"},{"mountpoint",mountPath}}});
            disk->setProperty("disks",QVariant::fromValue(fixture));disk->setProperty("remote",QVariant::fromValue(fixture));
            settle(window);
            auto *list=disk->findChild<QQuickItem *>("disksList");QVERIFY(list);QCOMPARE(list->property("count").toInt(),1);
            std::function<QQuickItem *(QQuickItem *)> visualFind=[&](QQuickItem *item)->QQuickItem *{
                if(item->objectName()=="disksOpenFiles")return item;
                for(auto *child:item->childItems())if(auto *found=visualFind(child))return found;
                return nullptr;
            };
            auto *open=visualFind(list);QVERIFY(open);QVERIFY(open->isVisible());
            QSignalSpy openPath(disk,SIGNAL(openPath(QString)));
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,open->mapToScene(QPointF(open->width()/2,open->height()/2)).toPoint());
            QCOMPARE(openPath.size(),1);QCOMPARE(openPath.first().first().toString(),mountPath);
            disk->setProperty("section","devices");settle(window);
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-disks.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
        if(preview=="terminal") {
            auto *term=shell->findChild<QQuickItem *>("terminalWindow");QVERIFY(term);
            QVERIFY(term->isVisible());
            QVERIFY(term->x()>=0 && term->y()>=47);
            QVERIFY(term->x()+term->width()<=shell->width());
            auto *sidebar=term->findChild<QQuickItem *>("terminalSidebar");QVERIFY(sidebar);
            QCOMPARE(sidebar->isVisible(),!term->property("drawer").toBool());
            auto *connectButton=term->findChild<QQuickItem *>("terminalConnectButton");QVERIFY(connectButton);
            QVERIFY(connectButton->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,connectButton->mapToScene(QPointF(connectButton->width()/2,connectButton->height()/2)).toPoint());
            QTRY_V(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
            auto *surface=term->findChild<QQuickItem *>("terminalSurface");QVERIFY(surface);
            surface->setVisible(true); // exercise VT input independently of a real SSH fixture
            surface->setProperty("inputEnabled",true);
            QSignalSpy bytes(surface,SIGNAL(bytes(QByteArray)));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[2J\033[HHELLO\r\n\033[31mRED\033[0m"))));
            QString text; QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));
            QVERIFY(text.contains("HELLO") && text.contains("RED") && !text.contains(QChar(27)));
            surface->forceActiveFocus();
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier|Qt::ShiftModifier);
            QTRY_V(term->property("searchOpen").toBool());
            auto *searchInput=term->findChild<QQuickItem *>("terminalSearchInput");QVERIFY(searchInput);
            QVERIFY(searchInput->hasActiveFocus());
            for(char c:QByteArray("HELLO"))QTest::keyClick(window,c);
            QTRY_C(surface->property("searchStatus").toString(),QString("1 / 1"));
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("HELLO"));
            QVERIFY(bytes.isEmpty());
            QTest::keyClick(window,Qt::Key_Escape);QVERIFY(!term->property("searchOpen").toBool());
            QCOMPARE(shell->property("foregroundApp").toString(),QString("terminal"));QVERIFY(term->isVisible());
            QVERIFY(surface->hasActiveFocus());
            settle(window); // settle the search bar's adaptive grid resize before dragging
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier);
            QVERIFY(!bytes.isEmpty());QCOMPARE(bytes.last().first().toByteArray(),QByteArray(1,char(6)));bytes.clear();
            const qreal cw=surface->width()/surface->property("columns").toInt();
            const qreal ch=surface->height()/surface->property("rows").toInt();
            auto select=[&](qreal x1,qreal y1,qreal x2,qreal y2) {
                const QPoint from=surface->mapToScene(QPointF(x1*cw,y1*ch)).toPoint();
                const QPoint to=surface->mapToScene(QPointF(x2*cw,y2*ch)).toPoint();
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
                QTest::mouseMove(window,to);
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
            };
            select(0.5,0.5,4.5,0.5);
            QVERIFY(surface->property("hasSelection").toBool());
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("HELLO"));
            QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier|Qt::ShiftModifier);
            QCOMPARE(QGuiApplication::clipboard()->text(),QString("HELLO"));QVERIFY(bytes.isEmpty());
            select(2.5,1.5,2.5,0.5);
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("LLO\nRED"));
            QVERIFY(bytes.isEmpty());
            surface->forceActiveFocus();QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
            QVERIFY(!bytes.isEmpty());QCOMPARE(bytes.last().first().toByteArray(),QByteArray(1,char(3)));
            QVERIFY(!surface->property("hasSelection").toBool());
            bytes.clear();
            const QString command=QString::fromUtf8("printf 'été Привет'");
            auto *history=term->findChild<QQuickItem *>("terminalHistory");QVERIFY(history);
            history->setProperty("model",QVariantList{QVariantMap{{"text",command}}});
            term->setProperty("navigationOpen",true);settle(window);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,history->mapToScene(QPointF(30,12)).toPoint());
            QByteArray typed;for(const auto &event:bytes)typed+=event.first().toByteArray();
            QCOMPARE(typed,command.toUtf8());QVERIFY(surface->hasActiveFocus());
            for(const QString &unsafe:QStringList{QString("pwd\nexit"),QString("pwd\rexit"),QString(QChar(27))+"[A",QString(QChar::Null),QString(16001,'x')}) {
                bytes.clear();QVERIFY(QMetaObject::invokeMethod(surface,"insertHistory",Q_ARG(QString,unsafe)));QVERIFY(bytes.isEmpty());
            }
            surface->setProperty("inputEnabled",false);bytes.clear();
            QVERIFY(QMetaObject::invokeMethod(surface,"insertHistory",Q_ARG(QString,command)));QVERIFY(bytes.isEmpty());
            surface->setProperty("inputEnabled",true);

            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[?1049hALT\033[?1049l"))));
            QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));QVERIFY(text.contains("HELLO"));
            QVERIFY(QMetaObject::invokeMethod(surface,"reset"));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QString::fromUtf8("été 界é\033[8mSECRET\033[0m").toUtf8())));
            select(0.5,0.5,6.5,0.5);
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString::fromUtf8("été 界é"));
            QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));QVERIFY(!text.contains("SECRET"));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("new"))));
            QVERIFY(!surface->property("hasSelection").toBool());
            QVERIFY(QMetaObject::invokeMethod(surface,"reset"));
            QByteArray output;
            const int rows=surface->property("rows").toInt();
            for(int i=0;i<rows+15;++i)output+=QString("line%1\r\n").arg(i,4,10,QChar('0')).toUtf8();
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,output)));
            QVERIFY(surface->property("scrollbackLines").toInt()>0);
            auto *outputScroll=term->findChild<QQuickItem *>("terminalOutputScroll");QVERIFY(outputScroll);
            QTRY_V(outputScroll->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,outputScroll->mapToScene(QPointF(outputScroll->width()/2,5)).toPoint());
            QTRY_V(surface->property("scrollOffset").toInt()>0);
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,0)));
            bytes.clear();QTest::keyClick(window,Qt::Key_PageUp,Qt::ShiftModifier);
            QVERIFY(surface->property("scrollOffset").toInt()>0);QVERIFY(bytes.isEmpty());
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,10000)));
            QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));
            QVERIFY(text.startsWith("line0000\n"));
            const QString anchored=text;
            select(0.5,0.5,7.5,0.5);
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("line0000"));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("new output\r\n"))));
            QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,anchored);
            QVERIFY(!surface->property("hasSelection").toBool());
            QTest::keyClick(window,Qt::Key_End,Qt::ShiftModifier);
            QCOMPARE(surface->property("scrollOffset").toInt(),0);QVERIFY(bytes.isEmpty());
            const QPointF wheelPosition=surface->mapToScene(QPointF(20,20));
            QWheelEvent wheel(wheelPosition,window->mapToGlobal(wheelPosition.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&wheel);
            QCOMPARE(surface->property("scrollOffset").toInt(),3);QVERIFY(bytes.isEmpty());
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,0)));
            const qreal oldHeight=surface->height();
            const int beforeResize=surface->property("scrollbackLines").toInt();
            surface->setHeight(oldHeight+40);
            QVERIFY(surface->property("scrollbackLines").toInt()<beforeResize);
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,10000)));
            QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));QVERIFY(text.startsWith("line0000\n"));
            surface->setHeight(oldHeight);
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,0)));
            const int saved=surface->property("scrollbackLines").toInt();
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[?1049hALT\r\n"))));
            QCOMPARE(surface->property("scrollbackLines").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,10000)));
            QCOMPARE(surface->property("scrollOffset").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[?1049l"))));
            QCOMPARE(surface->property("scrollbackLines").toInt(),saved);
            output.clear();for(int i=0;i<1100;++i)output+="bounded output\r\n";
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,output)));
            QCOMPARE(surface->property("scrollbackLines").toInt(),1000);
            QVERIFY(QMetaObject::invokeMethod(surface,"scrollTo",Q_ARG(int,10000)));
            QCOMPARE(surface->property("scrollOffset").toInt(),1000);
            QVERIFY(QMetaObject::invokeMethod(surface,"insertHistory",Q_ARG(QString,QString("echo typed"))));
            QCOMPARE(surface->property("scrollOffset").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[3J"))));
            QCOMPARE(surface->property("scrollbackLines").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,output)));
            QVERIFY(QMetaObject::invokeMethod(surface,"reset"));
            QCOMPARE(surface->property("scrollbackLines").toInt(),0);
            QCOMPARE(surface->property("scrollOffset").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QString::fromUtf8("Alpha café\r\nbeta ALPHA\r\n界é\033[8mSECRET\033[0m").toUtf8())));
            auto find=[&](const QString &query,bool backward=false){return QMetaObject::invokeMethod(surface,"findText",Q_ARG(QString,query),Q_ARG(bool,backward));};
            bytes.clear();QVERIFY(find("alpha"));QCOMPARE(surface->property("searchStatus").toString(),QString("1 / 2"));
            QVERIFY(find("alpha"));QCOMPARE(surface->property("searchStatus").toString(),QString("2 / 2"));
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("ALPHA"));
            QVERIFY(find("alpha"));QCOMPARE(surface->property("searchStatus").toString(),QString("1 / 2"));
            QVERIFY(find("alpha",true));QCOMPARE(surface->property("searchStatus").toString(),QString("2 / 2"));
            QVERIFY(find(QString::fromUtf8("CAFÉ")));QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString::fromUtf8("café"));
            QVERIFY(find(QString::fromUtf8("界é")));QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString::fromUtf8("界é"));
            QVERIFY(find("SECRET"));QCOMPARE(surface->property("searchStatus").toString(),QString("Aucun résultat"));
            QVERIFY(!surface->property("hasSelection").toBool());
            QVERIFY(find("alpha"));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,output)));
            QVERIFY(surface->property("searchStatus").toString().contains("relancer"));
            QVERIFY(find("bounded"));QVERIFY(surface->property("scrollOffset").toInt()>0);
            QVERIFY(surface->property("searchStatus").toString().contains("1000"));
            QVERIFY(find("o"));QVERIFY(surface->property("searchStatus").toString().contains("limite"));
            QVERIFY(find("bad\nquery"));QVERIFY(surface->property("searchStatus").toString().contains("256"));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[?1049hALT only"))));
            QVERIFY(find("bounded"));QCOMPARE(surface->property("searchStatus").toString(),QString("Aucun résultat"));
            QVERIFY(find("ALT"));QCOMPARE(surface->property("searchStatus").toString(),QString("1 / 1"));
            QVERIFY(bytes.isEmpty());
            QVERIFY(QMetaObject::invokeMethod(surface,"reset"));QVERIFY(surface->property("searchStatus").toString().isEmpty());
        }
        window->resize(QSize(844,390)); settle(window);
        window->resize(size); settle(window);
        QCOMPARE(files->findChild<QQuickItem *>("fileViewer"), editor);
        if (preview == "editor") QVERIFY(editor->property("dirty").toBool());
        if (adaptive && preview == "editor") {
            QVERIFY(QMetaObject::invokeMethod(shell, "launch", Q_ARG(QVariant, QVariant("logs"))));
            QVERIFY(QMetaObject::invokeMethod(shell, "launch", Q_ARG(QVariant, QVariant("files"))));
            QVERIFY(editor->property("dirty").toBool());
            QTest::keyRelease(window, Qt::Key_Back);
            auto *dialog = editor->findChild<QQuickItem *>("unsavedDialog"); QVERIFY(dialog);
            QVERIFY(dialog->isVisible());
            QVERIFY(editor->property("dirty").toBool());
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-unsaved.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
        {   // Moves between apps (audit backlog 8): one router, bound to the server that was connected when the action was made.
            QQmlComponent routerComponent(&engine), sysComponent2(&engine);
            routerComponent.setData(kFakeRouter, QUrl()); sysComponent2.setData(kFakeSystem, QUrl());
            auto *router = routerComponent.create(); QVERIFY2(router, qPrintable(routerComponent.errorString()));
            auto *fakeSys2 = sysComponent2.create(); QVERIFY2(fakeSys2, qPrintable(sysComponent2.errorString()));
            router->setParent(window); fakeSys2->setParent(window);
            shell->setProperty("serversOpen", false);
            shell->setProperty("controller", QVariant::fromValue(router));
            shell->setProperty("systemController", QVariant::fromValue(fakeSys2));
            shell->setProperty("systemOpen", true);
            shell->setProperty("foregroundApp", "system");
            settle(window);
            auto *sysWin = shell->findChild<QQuickItem *>("systemWindow"); QVERIFY(sysWin); QVERIFY(sysWin->isVisible());
            auto *logsWin = shell->findChild<QQuickItem *>("logsWindow"); QVERIFY(logsWin);
            auto click = [&](QQuickItem *item) {
                QVERIFY(item); QVERIFY(item->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                settle(window);
            };
            auto toastText = [&] { auto *toast = findByName(shell, "routeToast"); return toast && toast->isVisible() ? toast->findChild<QQuickItem *>()->property("text").toString() : QString(); };
            auto route = [&](const QVariantMap &action) { QVariant r; [&] { QVERIFY(QMetaObject::invokeMethod(shell, "route", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, action))); }(); settle(window); return r.toBool(); };
            auto act = [&](const QString &type, const QVariantMap &fields) { QVariant r; [&] { QVERIFY(QMetaObject::invokeMethod(shell, "act", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, type), Q_ARG(QVariant, fields))); }(); settle(window); return r.toBool(); };

            // Système -> Journaux: a service row, then its journal.
            QVERIFY(QMetaObject::invokeMethod(shell, "raiseApp", Q_ARG(QVariant, QVariant("system"))));
            sysWin->setProperty("section", "services");
            sysWin->setProperty("filterText", "");
            settle(window);
            click(findByName(sysWin, "systemServiceFilter_all"));
            auto *serviceList = findByName(sysWin, "systemServiceList"); QVERIFY(serviceList);
            QQuickItem *row = nullptr;
            QTRY_V([&] {
                QMetaObject::invokeMethod(serviceList, "forceLayout");
                row = nullptr;
                for (int i = 0; i < 3 && !row; ++i) {
                    QQuickItem *candidate = nullptr;
                    QMetaObject::invokeMethod(serviceList, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, candidate), Q_ARG(int, i));
                    if (candidate && candidate->property("modelData").toMap().value("unit").toString() == "broken.service") row = candidate;
                }
                return row != nullptr;
            }());
            click(row);
            QCOMPARE(sysWin->property("selectedUnit").toString(), QString("broken.service"));
            auto *serviceDetail = findByName(sysWin, "systemServiceDetail"); QVERIFY(serviceDetail); QTRY_V(serviceDetail->isVisible());
            QVERIFY(serviceDetail->y() >= 0 && serviceDetail->y() + serviceDetail->height() <= sysWin->height());
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-service-detail.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
            click(findByName(serviceDetail, "systemServiceLogs"));
            QCOMPARE(shell->property("lastRoute").toString(), QString("showLogs@srvA"));
            QVERIFY(shell->property("logsOpen").toBool());
            QCOMPARE(logsWin->property("requestedUnit").toString(), QString("broken.service"));
            QCOMPARE(logsWin->property("view").toString(), QString("service"));

            // A stale action (made for srvA while srvB is connected) is refused and says so; the active server is not substituted.
            router->setProperty("activeServerId", "srvB");
            QVERIFY(!route({ { "type", "showLogs" }, { "serverId", "srvA" }, { "unit", "ssh.service" } }));
            QCOMPARE(logsWin->property("requestedUnit").toString(), QString("broken.service"));
            QCOMPARE(shell->property("lastRoute").toString(), QString("showLogs@srvA"));
            QVERIFY2(toastText().contains("ignorée"), qPrintable(toastText()));
            QVERIFY(!route({ { "type", "terminalHere" }, { "serverId", "" }, { "path", "/x" } }));      // no server named at all
            router->setProperty("connected", false);
            QVERIFY(!act("showService", { { "unit", "cron.service" } }));                              // nothing is connected
            QCOMPARE(sysWin->property("selectedUnit").toString(), QString("broken.service"));
            router->setProperty("connected", true); router->setProperty("activeServerId", "srvA");

            // Journaux -> Système: the service of an event, with its state.
            const int reloadsBefore = fakeSys2->property("reloads").toInt();
            QVERIFY(act("showService", { { "unit", "cron.service" } }));
            QCOMPARE(sysWin->property("section").toString(), QString("services"));
            QCOMPARE(sysWin->property("selectedUnit").toString(), QString("cron.service"));
            QCOMPARE(sysWin->property("filterText").toString(), QString("cron.service"));
            QVERIFY(fakeSys2->property("reloads").toInt() > reloadsBefore);

            // Copier: the state of the service and the details of a process reach the clipboard, and a notice says so.
            QGuiApplication::clipboard()->clear();
            click(findByName(serviceDetail, "systemCopyService"));
            QVERIFY2(QGuiApplication::clipboard()->text().contains("cron.service") && QGuiApplication::clipboard()->text().contains("inactive"), qPrintable(QGuiApplication::clipboard()->text()));
            QVERIFY2(toastText().startsWith("Copié"), qPrintable(toastText()));
            sysWin->setProperty("section", "processes"); sysWin->setProperty("filterText", ""); sysWin->setProperty("selectedPid", 700);
            settle(window);
            auto *processDetail = findByName(sysWin, "systemDetail"); QVERIFY(processDetail); QTRY_V(processDetail->isVisible());
            click(findByName(processDetail, "systemCopyProcess"));
            QVERIFY2(QGuiApplication::clipboard()->text().contains("nginx") && QGuiApplication::clipboard()->text().contains("PID 700"), qPrintable(QGuiApplication::clipboard()->text()));

            // Fichiers -> Terminal: the command is proposed, quoted, and waits for the user's confirmation.
            auto *terminalWin = shell->findChild<QQuickItem *>("terminalWindow"); QVERIFY(terminalWin);
            QVERIFY(act("terminalHere", { { "path", "/home/o'neil" } }));
            QVERIFY(shell->property("terminalOpen").toBool());
            QCOMPARE(terminalWin->property("pasteText").toString(), QString("cd -- '/home/o'\\''neil'"));   // typed, never run: Enter is the user's
            // Disques -> Fichiers.
            QVERIFY(act("revealInFiles", { { "path", "/mnt/data" } }));
            QCOMPARE(router->property("navigated").toStringList(), QStringList{ "/mnt/data" });
            QVERIFY(shell->property("filesOpen").toBool());

            shell->setProperty("systemOpen", false); shell->setProperty("logsOpen", false); shell->setProperty("terminalOpen", false); shell->setProperty("filesOpen", false);
            shell->setProperty("systemController", QVariant::fromValue<QObject *>(nullptr));
            shell->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
            settle(window);
        }
        {   // The server pill turns red and says so when the link is lost.
            auto *pill = findByName(shell, "sessionPill"); QVERIFY(pill);
            auto *bar = pill->parentItem(); QVERIFY(bar);
            bar->setProperty("connected", true); bar->setProperty("serverLabel", "fixture@server"); bar->setProperty("linkLost", false);
            settle(window);
            auto *dot = findByName(pill, "sessionDot"); QVERIFY(dot);
            auto *label = findByName(pill, "sessionPillText"); QVERIFY(label);
            QCOMPARE(dot->property("color").value<QColor>(), QColor("#4ADE80"));
            QVERIFY(!label->property("text").toString().contains("perdue"));
            bar->setProperty("linkLost", true);
            settle(window);
            QCOMPARE(dot->property("color").value<QColor>(), QColor("#F87171"));
            QVERIFY(label->property("text").toString().contains("connexion perdue"));
            QVERIFY(pill->x() >= 0 && pill->x() + pill->width() <= shell->width());
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-pill-lost.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
        for (const auto &entry : warnings) for (const auto &error : qvariant_cast<QList<QQmlError>>(entry.first()))
            QVERIFY2(!error.description().contains("Error") && !error.description().contains("Binding loop")
                     && !error.description().contains("Cannot assign"), qPrintable(error.toString()));
    }
};
QTEST_MAIN(ShellLayoutTest)
#include "ShellLayoutTest.moc"
