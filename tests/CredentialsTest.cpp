#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>
#include "core/RemoteController.h"
#include "core/SecretStore.h"

namespace fortiq {
// Remembered secrets: the protected store itself and the controller rules
// around it. Uses its own QSettings scope and uniquely named store entries,
// and deletes both afterwards.
class CredentialsTest : public QObject {
    Q_OBJECT
    QStringList m_keys;
    QString newKey() { m_keys << QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces); return m_keys.last(); }
    static void seedProfile(const QString &id, bool secretSaved) {
        QSettings settings;
        const QVariantMap p { { "id", id }, { "name", "srv" }, { "host", "host.example" }, { "port", 22 }, { "user", "debian" },
                              { "keyPath", "" }, { "secretSaved", secretSaved } };
        settings.setValue("servers/profiles", QJsonDocument(QJsonArray::fromVariantList({ p })).toJson(QJsonDocument::Compact));
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("CYBOU-tests");
        QCoreApplication::setApplicationName("fortiq-credentials");
        QSettings().clear();
    }
    void cleanupTestCase() {
        for (const QString &key : m_keys) SecretStore::remove(key);
        QSettings().clear();
    }
    void storeRoundTrip() {
#ifndef Q_OS_WIN
        QVERIFY(!SecretStore::available());
        QVERIFY(SecretStore::save("x", "y").unavailable);
        QVERIFY(!SecretStore::load("x").ok);
        return;
#else
        QVERIFY(SecretStore::available());
        const QString key = newKey();
        QVERIFY(SecretStore::load(key).notFound);
        const QString secret = QString::fromUtf8("pässwörd Привет \"quoted\" 'x' $(y) `z`");
        QVERIFY2(SecretStore::save(key, secret).ok, "save");
        const auto loaded = SecretStore::load(key);
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.secret, secret);
        // Overwrite, then delete; deleting twice is not an error.
        QVERIFY(SecretStore::save(key, "second").ok);
        QCOMPARE(SecretStore::load(key).secret, QString("second"));
        QVERIFY(SecretStore::remove(key).ok);
        QVERIFY(SecretStore::load(key).notFound);
        QVERIFY(SecretStore::remove(key).ok);
        QVERIFY(!SecretStore::save(key, QString()).ok);                       // nothing to store
        QVERIFY(!SecretStore::save(key, QString(5000, 'a')).ok);              // too long: explicit error
        QVERIFY(SecretStore::load(key).notFound);
#endif
    }
    void secretNeverReachesSettings() {
#ifdef Q_OS_WIN
        const QString id = newKey();
        seedProfile(id, false);
        QVERIFY(SecretStore::save(id, "TOPSECRET-VALUE").ok);
        QSettings settings;
        for (const QString &k : settings.allKeys())
            QVERIFY2(!settings.value(k).toString().contains("TOPSECRET-VALUE"), qPrintable(k));
#endif
    }
    void editKeepsOrDropsSecret() {
#ifdef Q_OS_WIN
        const QString id = newKey();
        seedProfile(id, true);
        QVERIFY(SecretStore::save(id, "pw").ok);
        RemoteController remote;
        // Renaming keeps the secret: same account on the same server.
        QCOMPARE(remote.saveServer({ { "id", id }, { "name", "renamed" }, { "host", "host.example" }, { "port", 22 }, { "user", "debian" } }), id);
        QVERIFY(remote.servers().first().toMap().value("secretSaved").toBool());
        QVERIFY(SecretStore::load(id).ok);
        // A different host or user must never receive it.
        QCOMPARE(remote.saveServer({ { "id", id }, { "name", "renamed" }, { "host", "evil.example" }, { "port", 22 }, { "user", "debian" } }), id);
        QVERIFY(!remote.servers().first().toMap().value("secretSaved").toBool());
        QVERIFY(SecretStore::load(id).notFound);
#endif
    }
    void removeServerDeletesSecret() {
#ifdef Q_OS_WIN
        const QString id = newKey();
        seedProfile(id, true);
        QVERIFY(SecretStore::save(id, "pw").ok);
        RemoteController remote;
        remote.removeServer(id);
        QVERIFY(SecretStore::load(id).notFound);
        QVERIFY(remote.servers().isEmpty());
#endif
    }
    void forgetAndStaleFlag() {
#ifdef Q_OS_WIN
        const QString id = newKey();
        seedProfile(id, true);
        QVERIFY(SecretStore::save(id, "pw").ok);
        RemoteController remote;
        remote.forgetSecret(id);
        QVERIFY(SecretStore::load(id).notFound);
        QVERIFY(!remote.servers().first().toMap().value("secretSaved").toBool());
        // The flag says "saved" but the entry is gone: say so, drop the flag, do not connect.
        seedProfile(id, true);
        RemoteController again;
        again.connectToSaved(id);
        QVERIFY2(again.statusText().contains("no longer"), qPrintable(again.statusText()));
        QVERIFY(!again.servers().first().toMap().value("secretSaved").toBool());
        QVERIFY(!again.busy());
#endif
    }
    void rememberWithoutStoreIsRefused() {
        RemoteController remote;
        if (remote.secretStoreAvailable()) QSKIP("A protected store exists on this platform");
        seedProfile("a", false);
        remote.connectToRemembering("a", "pw", true);
        QVERIFY(remote.statusText().contains("not saved"));
        QVERIFY(!remote.busy());
    }
};
}

QTEST_GUILESS_MAIN(fortiq::CredentialsTest)
#include "CredentialsTest.moc"
