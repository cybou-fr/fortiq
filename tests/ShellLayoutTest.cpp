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
#ifndef Q_MOC_RUN
#include "../src/documents/MarkdownPreview.h"
#endif
Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

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
        window->resize(size); QTest::qWait(200);
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
            QTest::qWait(50);
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
            QTest::qWait(30);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                files->mapToScene(QPointF(100, 20)).toPoint());
            QTRY_VERIFY(files->z() > logs->z());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                logs->mapToScene(QPointF(25, 80)).toPoint());
            QTRY_VERIFY(logs->z() > files->z());
            const auto logPosition=logs->position();
            const auto logStart=logs->mapToScene(QPointF(80,560)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,logStart);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,logStart+QPoint(60,40));
            QVERIFY(logs->x()>logPosition.x()); QVERIFY(logs->y()>logPosition.y());
            logs->setPosition(logPosition);
            QVERIFY(QMetaObject::invokeMethod(shell, "launch", Q_ARG(QVariant, QVariant("files"))));
            QTRY_VERIFY(files->z() > logs->z());
            // The passive activation handler must not consume child clicks.
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                files->mapToScene(QPointF(20, 23)).toPoint());
            QVERIFY(!shell->property("filesOpen").toBool());
            shell->setProperty("filesOpen", true);
            shell->setProperty("logsOpen", false);
            shell->setProperty("launcherOpen", true);
            QTest::qWait(50);
            auto *launcher = shell->findChild<QQuickItem *>("launcherPanel"); QVERIFY(launcher);
            QVERIFY(launcher->x() >= 0);
            QVERIFY(launcher->y() + launcher->height() < shell->height() - 70);
            QCOMPARE(launcher->x(), (shell->width() - qMin<qreal>(shell->width() - 16, 9 * 60 + 84)) / 2); // bottom panel: 9 apps outside the fixture
            shell->setProperty("controlOpen", true);
            QTest::qWait(50);
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
                QTRY_COMPARE(shell->property("wallpaperName").toString(), (QStringList{"aurora", "desert", "city"}.at(index)));
            }
            QVERIFY(control->height() < 800);                           // fits its content, no empty tail
            // Presses inside the panel keep it open; a press elsewhere closes it and the launcher.
            shell->setProperty("launcherOpen", true);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene(QPointF(150, 300)).toPoint());
            QVERIFY(shell->property("controlOpen").toBool());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 700));
            QTRY_VERIFY(!shell->property("controlOpen").toBool());
            QTRY_VERIFY(!shell->property("launcherOpen").toBool());
            shell->setProperty("controlOpen", true);
            QTest::qWait(400);
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-control-selected.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
            {   // Login screen: connecting works like logging in to Linux, and logout returns to it.
                QQmlComponent fakeComponent(&engine);
                fakeComponent.setData(kFakeController, QUrl());
                auto *fake = fakeComponent.create(); QVERIFY2(fake, qPrintable(fakeComponent.errorString())); fake->setParent(window);
                shell->setProperty("serversOpen", true);
                QTest::qWait(100);
                auto *login = shell->findChild<QQuickItem *>("serverManager"); QVERIFY(login);
                login->setProperty("controller", QVariant::fromValue(fake));   // only the login screen sees the fake
                QTest::qWait(50);
                QVERIFY(login->isVisible());
                QCOMPARE(login->width(), shell->width()); QCOMPARE(login->height(), shell->height());   // replaces the desktop
                auto *secretInput = findByName(login, "loginSecret"); QVERIFY(secretInput); QVERIFY(secretInput->isVisible());
                QTest::qWait(300);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-login.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                secretInput->forceActiveFocus();
                for (const char c : {'p', 'w', '1'}) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_COMPARE(fake->property("calls").toStringList(), QStringList{"a:pw1"});
                // Clicking a key account connects at once.
                auto *keyTile = findByName(login, "loginTile_b"); QVERIFY(keyTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, keyTile->mapToScene(QPointF(keyTile->width() / 2, 50)).toPoint());
                QTRY_COMPARE(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:"}));
                // A remembered secret connects at once through the protected store; no typing needed.
                auto *savedTile = findByName(login, "loginTile_c"); QVERIFY(savedTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, savedTile->mapToScene(QPointF(savedTile->width() / 2, 50)).toPoint());
                QTRY_COMPARE(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:", "saved:c"}));
                QVERIFY(findByName(login, "loginForget")->isVisible());
                // "Se souvenir" sends the typed secret to the controller with remember = true.
                auto *plainTile = findByName(login, "loginTile_a"); QVERIFY(plainTile);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, plainTile->mapToScene(QPointF(plainTile->width() / 2, 50)).toPoint());
                auto *box = findByName(login, "loginRemember"); QVERIFY(box); QVERIFY(box->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint());
                secretInput->forceActiveFocus();
                for (const char c : {'p', 'w', '2'}) QTest::keyClick(window, c);
                QTest::keyClick(window, Qt::Key_Return);
                QTRY_COMPARE(fake->property("calls").toStringList(), (QStringList{"a:pw1", "b:", "saved:c", "remember:a:pw2"}));
                // Logout from the session menu closes the windows and returns to the login screen.
                login->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
                shell->setProperty("serversOpen", false);
                shell->setProperty("filesOpen", true);
                shell->setProperty("sessionMenuOpen", true);
                QTest::qWait(50);
                QVERIFY(window->grabWindow().save(QString("layout-captures/%1-session.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
                auto *pill = findByName(shell, "sessionPill"); QVERIFY(pill); QVERIFY(pill->isVisible());
                QVERIFY(pill->x() + pill->width() > shell->width() - 20);                  // at the right, where the avatar was
                shell->setProperty("sessionMenuOpen", false);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pill->mapToScene(QPointF(pill->width() / 2, pill->height() / 2)).toPoint());
                QTRY_VERIFY(shell->property("serversOpen").toBool());   // not connected: the pill leads to the login screen
                shell->setProperty("serversOpen", false);
                shell->setProperty("sessionMenuOpen", true);
                QTest::qWait(50);
                auto *logout = findByName(shell, "logoutButton"); QVERIFY(logout); QVERIFY(logout->isVisible());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, logout->mapToScene(QPointF(logout->width() / 2, logout->height() / 2)).toPoint());
                QTRY_VERIFY(shell->property("serversOpen").toBool());
                QVERIFY(!shell->property("filesOpen").toBool());
                QVERIFY(!shell->property("sessionMenuOpen").toBool());
                shell->setProperty("serversOpen", false);
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
            QTRY_VERIFY(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
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
            QTest::qWait(220);QString rendered;
            QVERIFY(QMetaObject::invokeMethod(render,"plainText",Q_RETURN_ARG(QString,rendered)));QVERIFY(rendered.contains("Title") && rendered.contains("bold") && !rendered.contains("**bold**"));
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
            QTest::qWait(220);
            QVERIFY(window->grabWindow().save(QString("layout-captures/%1-mdok.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
        if(preview=="disks") {
            auto *disk=shell->findChild<QQuickItem *>("disksWindow");QVERIFY(disk);QVERIFY(disk->isVisible());
            QVERIFY(disk->x()>=0 && disk->y()>=47);QVERIFY(disk->x()+disk->width()<=shell->width());
            auto *sidebar=disk->findChild<QQuickItem *>("disksSidebar");QVERIFY(sidebar);
            QCOMPARE(sidebar->isVisible(),!disk->property("drawer").toBool());
            auto *button=disk->findChild<QQuickItem *>("disksConnectButton");QVERIFY(button);QVERIFY(button->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
            QTRY_VERIFY(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
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
            QTest::qWait(30);
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
            disk->setProperty("section","devices");QTest::qWait(30);
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
            QTRY_VERIFY(shell->property("serversOpen").toBool());shell->setProperty("serversOpen",false);
            auto *surface=term->findChild<QQuickItem *>("terminalSurface");QVERIFY(surface);
            surface->setVisible(true); // exercise VT input independently of a real SSH fixture
            surface->setProperty("inputEnabled",true);
            QSignalSpy bytes(surface,SIGNAL(bytes(QByteArray)));
            QVERIFY(QMetaObject::invokeMethod(surface,"feed",Q_ARG(QByteArray,QByteArray("\033[2J\033[HHELLO\r\n\033[31mRED\033[0m"))));
            QString text; QVERIFY(QMetaObject::invokeMethod(surface,"screenText",Q_RETURN_ARG(QString,text)));
            QVERIFY(text.contains("HELLO") && text.contains("RED") && !text.contains(QChar(27)));
            surface->forceActiveFocus();
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier|Qt::ShiftModifier);
            QTRY_VERIFY(term->property("searchOpen").toBool());
            auto *searchInput=term->findChild<QQuickItem *>("terminalSearchInput");QVERIFY(searchInput);
            QVERIFY(searchInput->hasActiveFocus());
            for(char c:QByteArray("HELLO"))QTest::keyClick(window,c);
            QTRY_COMPARE(surface->property("searchStatus").toString(),QString("1 / 1"));
            QVERIFY(QMetaObject::invokeMethod(surface,"selectedText",Q_RETURN_ARG(QString,text)));QCOMPARE(text,QString("HELLO"));
            QVERIFY(bytes.isEmpty());
            QTest::keyClick(window,Qt::Key_Escape);QVERIFY(!term->property("searchOpen").toBool());
            QCOMPARE(shell->property("foregroundApp").toString(),QString("terminal"));QVERIFY(term->isVisible());
            QVERIFY(surface->hasActiveFocus());
            QTest::qWait(30); // settle the search bar's adaptive grid resize before dragging
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
            term->setProperty("navigationOpen",true);QTest::qWait(30);
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
            QTRY_VERIFY(outputScroll->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,outputScroll->mapToScene(QPointF(outputScroll->width()/2,5)).toPoint());
            QTRY_VERIFY(surface->property("scrollOffset").toInt()>0);
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
        window->resize(QSize(844,390)); QTest::qWait(30);
        window->resize(size); QTest::qWait(30);
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
        for (const auto &entry : warnings) for (const auto &error : qvariant_cast<QList<QQmlError>>(entry.first()))
            QVERIFY2(!error.description().contains("Error") && !error.description().contains("Binding loop")
                     && !error.description().contains("Cannot assign"), qPrintable(error.toString()));
    }
};
QTEST_MAIN(ShellLayoutTest)
#include "ShellLayoutTest.moc"
