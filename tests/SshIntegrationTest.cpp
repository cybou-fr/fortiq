// Live SSH/SFTP check against a disposable test server (e.g. the WSL
// `fortiq-test` distro). Skipped unless FORTIQ_IT_HOST, FORTIQ_IT_PORT,
// FORTIQ_IT_USER, FORTIQ_IT_PASSWORD and FORTIQ_IT_FINGERPRINT (the host key
// read independently on the server) are set; FORTIQ_IT_KEY adds an unencrypted
// private-key login check. Secrets come only from the
// environment and are never printed.
#include "ssh/SshClient.h"

#include <QtTest>

using fortiq::ConnectionRequest;
using fortiq::SshClient;
using fortiq::SshResult;

class SshIntegrationTest : public QObject {
    Q_OBJECT

    ConnectionRequest base;
    QString fingerprint;

    static QStringList names(const SshResult &result) {
        QStringList out;
        for (const QVariant &entry : result.entries) out << entry.toMap().value("name").toString();
        return out;
    }

private slots:
    void initTestCase() {
        if (qEnvironmentVariableIsEmpty("FORTIQ_IT_HOST")) QSKIP("FORTIQ_IT_* not set");
        base.host = qEnvironmentVariable("FORTIQ_IT_HOST");
        base.port = qEnvironmentVariableIntValue("FORTIQ_IT_PORT");
        base.user = qEnvironmentVariable("FORTIQ_IT_USER");
        base.password = qEnvironmentVariable("FORTIQ_IT_PASSWORD");
        fingerprint = qEnvironmentVariable("FORTIQ_IT_FINGERPRINT");
    }

    void unknownHostRequiresTrust() {
        const SshResult result = SshClient::list(base);
        QCOMPARE(result.status, SshResult::Status::TrustRequired);
        QCOMPARE(result.fingerprint, fingerprint);
    }

    void changedKeyIsRejected() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = QStringLiteral("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        QCOMPARE(SshClient::list(request).status, SshResult::Status::Error);
    }

    void wrongPasswordFails() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        request.password += QStringLiteral("-wrong");
        QCOMPARE(SshClient::list(request).status, SshResult::Status::Error);
    }

    void privateKeyAuthenticates() {
        if (qEnvironmentVariableIsEmpty("FORTIQ_IT_KEY")) QSKIP("FORTIQ_IT_KEY not set");
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        request.password.clear();
        request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        const SshResult result = SshClient::list(request);
        QVERIFY2(result.status == SshResult::Status::Ok, qPrintable(result.message));
    }

    void pinnedHostListsHomeWithUnicodeNames() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        const SshResult result = SshClient::list(request);
        QVERIFY2(result.status == SshResult::Status::Ok, qPrintable(result.message));
        const QStringList listed = names(result);
        QVERIFY(listed.contains(QStringLiteral("Mes documents")));
        QVERIFY(listed.contains(QStringLiteral("Notes été.txt")));
    }
};

QTEST_GUILESS_MAIN(SshIntegrationTest)
#include "SshIntegrationTest.moc"
