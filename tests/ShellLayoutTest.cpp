#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
#include <QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

class ShellLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QCoreApplication::setOrganizationName("CYBOU-tests"); QCoreApplication::setApplicationName("fortiq-layout"); }
    void viewport_data() {
        QTest::addColumn<QSize>("size"); QTest::addColumn<QString>("preview");
        for (const QSize size : {QSize(1536,1024), QSize(1024,768), QSize(768,1024), QSize(390,844)})
            for (const QString &preview : {QString(), QString("logs"), QString("editor"), QString("incident"), QString("service"), QString("servers")})
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
        auto *files = shell->findChild<QQuickItem *>("filesWindow"); QVERIFY(files);
        auto *logs = shell->findChild<QQuickItem *>("logsWindow"); QVERIFY(logs);
        auto *editor = files->findChild<QQuickItem *>("fileViewer"); QVERIFY(editor);
        const bool logApp = preview == "logs" || preview == "incident" || preview == "service";
        const bool adaptive = size.width() < 1280 || size.height() > size.width();
        QCOMPARE(shell->property("adaptive").toBool(), adaptive);
        if (adaptive) {
            QCOMPARE(shell->scale(), 1.0);
            for (auto *item : {files, logs}) {
                QVERIFY(item->x() >= 0 && item->y() >= 47);
                QVERIFY(item->x() + item->width() <= shell->width());
                QVERIFY(item->y() + item->height() <= shell->height() - 68);
            }
            QCOMPARE(files->isVisible(), !logApp);
            QCOMPARE(logs->isVisible(), logApp);
        } else {
            QCOMPARE(files->x(), 471.0); QCOMPARE(files->y(), 72.0);
            QCOMPARE(files->width(), 717.0); QCOMPARE(files->height(), 403.0);
        }
        if (preview == "servers") {
            auto *manager = shell->findChild<QQuickItem *>("serverManager"); QVERIFY(manager);
            shell->setProperty("serversOpen", true); manager->setProperty("editingId", "new");
            QTest::qWait(50);
        }
        const auto image = window->grabWindow(); QVERIFY(!image.isNull());
        QDir().mkpath("layout-captures");
        QVERIFY(image.save(QString("layout-captures/%1.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        if (preview == "incident" && adaptive && size.width() < 700) {
            auto *title = logs->findChild<QQuickItem *>("incidentTitle"); QVERIFY(title);
            auto *controls = logs->findChild<QQuickItem *>("incidentControls"); QVERIFY(controls);
            QVERIFY(title->y() + title->height() <= controls->y());
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
