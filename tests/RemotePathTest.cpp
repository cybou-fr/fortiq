#include "../src/core/RemotePath.h"
#include <QtTest/QTest>

class RemotePathTest : public QObject {
    Q_OBJECT
private slots:
    void safePath() {
        QCOMPARE(fortiq::safeChildPath("/home/user", "Reports 2026").value(), QString("/home/user/Reports 2026"));
        QCOMPARE(fortiq::safeChildPath("/", "home").value(), QString("/home"));
        QCOMPARE(fortiq::safeChildPath("/home/user", QString::fromUtf8("файл.txt")).value(), QString::fromUtf8("/home/user/файл.txt"));
        QVERIFY(!fortiq::safeChildPath("/home", "..").has_value());
        QVERIFY(!fortiq::safeChildPath("/home", "a/b").has_value());
        QVERIFY(!fortiq::safeChildPath("/home", "").has_value());
        QVERIFY(!fortiq::safeChildPath("relative", "ok").has_value());
    }
    void cleanAbsolute() {
        QCOMPARE(fortiq::cleanAbsolutePath("/var//log/").value(), QString("/var/log"));
        QCOMPARE(fortiq::cleanAbsolutePath("/").value(), QString("/"));
        QCOMPARE(fortiq::cleanAbsolutePath(QString::fromUtf8("/home/u/Mes documents/été")).value(),
                 QString::fromUtf8("/home/u/Mes documents/été"));
        QVERIFY(!fortiq::cleanAbsolutePath("/etc/../root").has_value());
        QVERIFY(!fortiq::cleanAbsolutePath("/./x").has_value());
        QVERIFY(!fortiq::cleanAbsolutePath("var/log").has_value());
        QVERIFY(!fortiq::cleanAbsolutePath(QString("/a") + QChar::Null).has_value());
    }
    void parent() {
        QCOMPARE(fortiq::parentPath("/home/user/docs"), QString("/home/user"));
        QCOMPARE(fortiq::parentPath("/home"), QString("/"));
        QCOMPARE(fortiq::parentPath("/"), QString("/"));
    }
};
QTEST_MAIN(RemotePathTest)
#include "RemotePathTest.moc"
