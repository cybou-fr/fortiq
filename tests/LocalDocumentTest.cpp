#include "core/LocalDocument.h"
#include <QtTest>
#include <QBuffer>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>

using namespace fortiq;
class ProviderDevice : public QIODevice {
public:
    QByteArray *bytes;
    qint64 failAfter;
    ProviderDevice(QByteArray *data, qint64 limit = -1) : bytes(data), failAfter(limit) { open(WriteOnly); }
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 n) override {
        if (failAfter >= 0 && bytes->size() >= failAfter) return -1;
        const qint64 take = std::min<qint64>(3, failAfter < 0 ? n : std::min(n, failAfter - bytes->size()));
        bytes->append(data, take); return take;
    }
};
class LocalDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void privateStagingIsWritableAndRemoved() {
        QStandardPaths::setTestModeEnabled(true);
        const auto pathTemplate = documentStagingTemplate();
        QVERIFY(!pathTemplate.isEmpty());
        QVERIFY(QDir::isAbsolutePath(pathTemplate));
        const auto relative = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).relativeFilePath(pathTemplate);
        QVERIFY(!relative.startsWith("..") && !QDir::isAbsolutePath(relative));
        QString path;
        {
            QTemporaryFile stage(pathTemplate);
            QVERIFY2(stage.open(), qPrintable(stage.errorString()));
            path = stage.fileName();
            QCOMPARE(stage.write("verified download"), 17);
            QVERIFY(stage.seek(0));
            QCOMPARE(stage.readAll(), QByteArray("verified download"));
            QVERIFY(QFile::exists(path));
        }
        QVERIFY(!QFile::exists(path));
        QStandardPaths::setTestModeEnabled(false);
    }
    void locationsAreOpaque() {
        const QUrl uri("content://com.android.providers.downloads.documents/document/primary%3AEt%C3%A9%2Fnotes%20%231.txt");
        QCOMPARE(localDocumentPath(uri), uri.toString(QUrl::FullyEncoded));
        QVERIFY(isContentDocument(localDocumentPath(uri)));
#ifdef Q_OS_WIN
        QCOMPARE(localDocumentPath(QUrl::fromLocalFile(QString::fromUtf8("C:/Mes documents/été #1.txt"))), QString::fromUtf8("C:/Mes documents/été #1.txt"));
#else
        QCOMPARE(localDocumentPath(QUrl::fromLocalFile(QString::fromUtf8("/home/u/Mes documents/été #1.txt"))), QString::fromUtf8("/home/u/Mes documents/été #1.txt"));
#endif
        QVERIFY(localDocumentPath(QUrl("https://example.test/key")).isEmpty());
        QVERIFY(localDocumentPath(QUrl("content:///no-provider")).isEmpty());
        QVERIFY(localDocumentPath(QUrl("content://provider/doc#fragment")).isEmpty());
    }
    void providerOpenedOnlyAtPublication() {
        QByteArray source("verified complete download"), result;
        QBuffer stage(&source); QVERIFY(stage.open(QIODevice::ReadWrite)); stage.seek(7);
        bool opened = false;
        auto r = exportDocument(stage, [&]() { opened = true; return std::make_unique<ProviderDevice>(&result); });
        QVERIFY(r.ok && r.attempted && opened);
        QCOMPARE(result, source); // partial writes are retried without losing bytes
    }
    void cancelBeforePublicationDoesNotOpenProvider() {
        QByteArray source("verified"), result;
        QBuffer stage(&source); QVERIFY(stage.open(QIODevice::ReadOnly));
        bool opened = false;
        const auto r = exportDocument(stage, [&]() { opened = true; return std::make_unique<ProviderDevice>(&result); }, [] { return false; });
        QVERIFY(!r.ok && r.cancelled && !r.attempted && !opened);
        QVERIFY(result.isEmpty());
    }
    void invalidStageDoesNotOpenProvider() {
        QBuffer stage;
        bool opened = false;
        const auto r = exportDocument(stage, [&]() -> std::unique_ptr<QIODevice> { opened = true; return {}; });
        QVERIFY(!r.ok && !r.attempted && !opened);
    }
    void failedProviderMayHavePartialData() {
        QByteArray source("verified complete"), result;
        QBuffer stage(&source); QVERIFY(stage.open(QIODevice::ReadOnly));
        const auto r = exportDocument(stage, [&]() { return std::make_unique<ProviderDevice>(&result, 5); });
        QVERIFY(!r.ok && r.attempted);
        QCOMPARE(result, source.left(5));
    }
    void failedOpenIsUncertain() {
        QByteArray source("verified"); QBuffer stage(&source); QVERIFY(stage.open(QIODevice::ReadOnly));
        const auto r = exportDocument(stage, []() -> std::unique_ptr<QIODevice> { return {}; });
        QVERIFY(!r.ok && r.attempted);
    }
    void cancelDuringExportIsUncertain() {
        QByteArray source(100000, 'x'), result; QBuffer stage(&source); QVERIFY(stage.open(QIODevice::ReadOnly));
        int calls = 0;
        const auto r = exportDocument(stage, [&]() { return std::make_unique<ProviderDevice>(&result); }, [&] { return ++calls < 3; });
        QVERIFY(!r.ok && r.attempted && r.cancelled);
        QVERIFY(!result.isEmpty() && result.size() < source.size());
    }
};
QTEST_GUILESS_MAIN(LocalDocumentTest)
#include "LocalDocumentTest.moc"
