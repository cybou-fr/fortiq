#include <QtConcurrent>
#include <QtTest>

#include "ssh/LinkMonitor.h"

using fortiq::LinkMonitor;

// LinkMonitor: the link state of each server, reported from worker threads, read in the main thread.
class LinkMonitorTest : public QObject {
    Q_OBJECT
private slots:
    void keyIsStableAndCaseInsensitiveOnHost() {
        QCOMPARE(LinkMonitor::key("Example.ORG", 22, "me"), QStringLiteral("me@example.org:22"));
        QVERIFY(LinkMonitor::key("a", 22, "u") != LinkMonitor::key("a", 2222, "u"));
        QVERIFY(LinkMonitor::key("a", 22, "u") != LinkMonitor::key("a", 22, "v"));
    }
    void aServerNeverReachedIsNotLost() {
        const QString key = LinkMonitor::key("never.example", 22, "u");
        LinkMonitor::reportLost(key, "x");
        QCoreApplication::processEvents();
        QVERIFY(!LinkMonitor::instance().lost(key));
        QVERIFY(!LinkMonitor::instance().state(key).everOk);
    }
    void lostAfterOkAndRestoredByOk() {
        const QString key = LinkMonitor::key("a.example", 22, "u");
        QSignalSpy spy(&LinkMonitor::instance(), &LinkMonitor::changed);
        LinkMonitor::reportOk(key);
        QCoreApplication::processEvents();
        QVERIFY(!LinkMonitor::instance().lost(key));
        LinkMonitor::reportLost(key, "Connexion SSH perdue.");
        QCoreApplication::processEvents();
        auto state = LinkMonitor::instance().state(key);
        QVERIFY(state.lost); QVERIFY(state.everOk); QVERIFY(!state.lostAt.isNull());
        QCOMPARE(state.reason, QStringLiteral("Connexion SSH perdue."));
        // A second report neither moves the time of the loss nor repeats the signal.
        const int signalsBefore = int(spy.size());
        const QDateTime firstLoss = state.lostAt;
        QTest::qWait(5);
        LinkMonitor::reportLost(key, "Serveur injoignable.");
        QCoreApplication::processEvents();
        state = LinkMonitor::instance().state(key);
        QCOMPARE(state.lostAt, firstLoss);
        QCOMPARE(int(spy.size()), signalsBefore);
        LinkMonitor::reportOk(key);
        QCoreApplication::processEvents();
        QVERIFY(!LinkMonitor::instance().lost(key));
        QVERIFY(LinkMonitor::instance().state(key).lostAt.isNull());
        QCOMPARE(int(spy.size()), signalsBefore + 1);
        // Plain successes in a healthy link are silent.
        LinkMonitor::reportOk(key); LinkMonitor::reportOk(key);
        QCoreApplication::processEvents();
        QCOMPARE(int(spy.size()), signalsBefore + 1);
    }
    void serversAreIndependent() {
        const QString a = LinkMonitor::key("a2.example", 22, "u"), b = LinkMonitor::key("b2.example", 22, "u");
        LinkMonitor::reportOk(a); LinkMonitor::reportOk(b);
        LinkMonitor::reportLost(a, "x");
        QCoreApplication::processEvents();
        QVERIFY(LinkMonitor::instance().lost(a));
        QVERIFY(!LinkMonitor::instance().lost(b));
    }
    void reportsFromWorkerThreadsArriveInOrder() {
        const QString key = LinkMonitor::key("t.example", 22, "u");
        QtConcurrent::run([key] {
            LinkMonitor::reportOk(key);
            LinkMonitor::reportLost(key, "x");
        }).waitForFinished();
        QTRY_VERIFY(LinkMonitor::instance().lost(key));
        QtConcurrent::run([key] { LinkMonitor::reportOk(key); }).waitForFinished();
        QTRY_VERIFY(!LinkMonitor::instance().lost(key));
        QCOMPARE(LinkMonitor::instance().thread(), QCoreApplication::instance()->thread());
    }
    void forgetClearsTheStateOfAServerLeftOnPurpose() {
        const QString key = LinkMonitor::key("f.example", 22, "u");
        LinkMonitor::reportOk(key); LinkMonitor::reportLost(key, "x");
        QCoreApplication::processEvents();
        QVERIFY(LinkMonitor::instance().lost(key));
        LinkMonitor::forget(key);
        QCoreApplication::processEvents();
        QVERIFY(!LinkMonitor::instance().lost(key));
        QVERIFY(!LinkMonitor::instance().state(key).everOk);
        LinkMonitor::reportLost(key, "x");     // after leaving on purpose a stray report is not a lost link
        QCoreApplication::processEvents();
        QVERIFY(!LinkMonitor::instance().lost(key));
    }
};

QTEST_MAIN(LinkMonitorTest)
#include "LinkMonitorTest.moc"
