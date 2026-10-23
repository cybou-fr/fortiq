#include <QtTest>
#include <QSignalSpy>
#include "core/TaskManager.h"

namespace fortiq {
class TaskManagerTest : public QObject {
    Q_OBJECT
    using S = TaskManager::State;
    static TaskManager::Spec spec(const QString &kind = "upload", bool cancellable = true, int generation = 1) {
        TaskManager::Spec s;
        s.kind = kind; s.title = "report.pdf"; s.source = "user@host"; s.destination = "/home/user";
        s.cancellable = cancellable; s.generation = generation;
        return s;
    }
private slots:
    void happyPath() {
        TaskManager m;
        QSignalSpy finished(&m, &TaskManager::finished);
        const int id = m.create(spec());
        QVERIFY(id > 0);
        QCOMPARE(m.state(id).value(), S::Queued);
        QVERIFY(m.start(id));
        QVERIFY(m.setProgress(id, 10, 100));
        QCOMPARE(m.task(id).value("done").toDouble(), 10.0);
        QVERIFY(m.succeed(id, "Published", 100));
        QCOMPARE(m.state(id).value(), S::Succeeded);
        QCOMPARE(m.task(id).value("state").toString(), QString("succeeded"));
        QCOMPARE(m.task(id).value("done").toDouble(), 100.0);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toString(), QString("succeeded"));
        QVERIFY(m.task(id).value("finished").toDouble() > 0);
        QCOMPARE(m.activeCount(), 0);
        QVERIFY(m.create(spec()) != id);   // ids are never reused
    }
    void terminalStatesAreFinal() {
        TaskManager m;
        for (int variant = 0; variant < 4; ++variant) {
            const int id = m.create(spec());
            QVERIFY(m.start(id));
            switch (variant) {
            case 0: QVERIFY(m.succeed(id, "ok")); break;
            case 1: QVERIFY(m.fail(id, "boom", "code")); break;
            case 2: QVERIFY(m.markUnknown(id, "maybe")); break;
            case 3: QVERIFY(m.markCancelled(id, "stop")); break;
            }
            const auto before = m.task(id);
            QVERIFY(!m.start(id));
            QVERIFY(!m.setProgress(id, 5, 5));
            QVERIFY(!m.setMessage(id, "late"));
            QVERIFY(!m.succeed(id, "again"));
            QVERIFY(!m.fail(id, "again"));
            QVERIFY(!m.markUnknown(id, "again"));
            QVERIFY(!m.markCancelled(id, "again"));
            QVERIFY(!m.requestCancel(id));
            QVERIFY(!m.requestDecision(id, "conflict", "x"));
            QCOMPARE(m.task(id), before);
        }
    }
    void finishedIsEmittedOncePerTask() {
        TaskManager m;
        QSignalSpy finished(&m, &TaskManager::finished);
        const int id = m.create(spec());
        m.start(id); m.fail(id, "x"); m.fail(id, "y"); m.markCancelled(id, "z");
        QCOMPARE(finished.size(), 1);
    }
    void failureCarriesMessageAndCode() {
        TaskManager m;
        const int id = m.create(spec());
        QVERIFY(m.fail(id, "Cannot create a file", "conflict"));   // may fail before it ever ran
        QCOMPARE(m.task(id).value("state").toString(), QString("failed"));
        QCOMPARE(m.task(id).value("message").toString(), QString("Cannot create a file"));
        QCOMPARE(m.task(id).value("code").toString(), QString("conflict"));
    }
    void queuedCannotSucceedOrBeUnknown() {
        TaskManager m;
        const int id = m.create(spec());
        QVERIFY(!m.succeed(id, "no"));
        QVERIFY(!m.markUnknown(id, "no"));
        QVERIFY(!m.setProgress(id, 1, 2));
        QCOMPARE(m.state(id).value(), S::Queued);
    }
    void cancelQueuedIsImmediate() {
        TaskManager m;
        QSignalSpy requested(&m, &TaskManager::cancelRequested);
        const int id = m.create(spec());
        QVERIFY(m.requestCancel(id));
        QCOMPARE(m.state(id).value(), S::Cancelled);
        QCOMPARE(requested.size(), 1);
        QVERIFY(!m.requestCancel(id));
    }
    void cancelRunningIsARequestNotAGuarantee() {
        TaskManager m;
        QSignalSpy requested(&m, &TaskManager::cancelRequested);
        const int id = m.create(spec());
        m.start(id);
        QVERIFY(m.requestCancel(id));
        QCOMPARE(m.state(id).value(), S::Cancelling);
        QCOMPARE(requested.size(), 1);
        QVERIFY(!m.requestCancel(id));           // already asked
        QCOMPARE(requested.size(), 1);
        QVERIFY(m.setProgress(id, 5, 10));        // still reports while stopping
        QCOMPARE(m.activeCount(), 1);
        // The work decides how it really ended: here it finished before the stop took effect.
        QVERIFY(m.succeed(id, "done anyway"));
        QCOMPARE(m.state(id).value(), S::Succeeded);
    }
    void cancellingCanEndAsUnknownOrCancelled() {
        TaskManager m;
        const int a = m.create(spec()), b = m.create(spec());
        m.start(a); m.start(b);
        m.requestCancel(a); m.requestCancel(b);
        QVERIFY(m.markUnknown(a, "connection lost"));
        QVERIFY(m.markCancelled(b, "stopped"));
        QCOMPARE(m.state(a).value(), S::Unknown);
        QCOMPARE(m.state(b).value(), S::Cancelled);
    }
    void nonCancellableTaskRefusesCancel() {
        TaskManager m;
        QSignalSpy requested(&m, &TaskManager::cancelRequested);
        const int id = m.create(spec("trash", false));
        m.start(id);
        QVERIFY(!m.requestCancel(id));
        QCOMPARE(m.state(id).value(), S::Running);
        QCOMPARE(requested.size(), 0);
        QVERIFY(m.succeed(id, "ok"));
    }
    void decisionFlow() {
        TaskManager m;
        const int id = m.create(spec());
        QVERIFY(!m.requestDecision(id, "conflict", "exists"));    // only a running task can ask
        m.start(id);
        m.setProgress(id, 40, 100);
        QVERIFY(m.requestDecision(id, "conflict", "name already exists"));
        QCOMPARE(m.state(id).value(), S::Queued);
        QCOMPARE(m.task(id).value("decision").toString(), QString("conflict"));
        QVERIFY(m.task(id).value("active").toBool());
        QVERIFY(!m.start(id));                                    // blocked until the user decides
        QVERIFY(m.resolveDecision(id));
        QCOMPARE(m.task(id).value("decision").toString(), QString());
        QCOMPARE(m.task(id).value("done").toDouble(), 0.0);
        QVERIFY(m.start(id));
        QVERIFY(m.succeed(id, "ok"));
        QVERIFY(!m.resolveDecision(id));
    }
    void cancelWhileWaitingForDecision() {
        TaskManager m;
        const int id = m.create(spec());
        m.start(id);
        m.requestDecision(id, "conflict", "exists");
        QVERIFY(m.requestCancel(id));
        QCOMPARE(m.state(id).value(), S::Cancelled);
        QCOMPARE(m.task(id).value("decision").toString(), QString());
    }
    void generationsAndCounts() {
        TaskManager m;
        const int a = m.create(spec("upload", true, 1)), b = m.create(spec("download", true, 2)), c = m.create(spec("save", false, 1));
        m.start(a); m.start(c);
        QCOMPARE(m.activeCount(), 3);
        QCOMPARE(m.activeIds(1), (QList<int>{ a, c }));
        QCOMPARE(m.activeIds(2), (QList<int>{ b }));
        QCOMPARE(m.activeIds(3), QList<int>());
        m.succeed(a, "ok");
        QCOMPARE(m.activeIds(1), (QList<int>{ c }));
        QCOMPARE(m.activeIds().size(), 2);
    }
    void clearFinishedKeepsActive() {
        TaskManager m;
        QSignalSpy changed(&m, &TaskManager::tasksChanged);
        const int done = m.create(spec()), running = m.create(spec()), waiting = m.create(spec());
        m.start(done); m.succeed(done, "ok");
        m.start(running);
        changed.clear();
        m.clearFinished();
        QCOMPARE(changed.size(), 1);
        QVERIFY(!m.exists(done));
        QVERIFY(m.exists(running)); QVERIFY(m.exists(waiting));
        QCOMPARE(m.count(), 2);
        m.clearFinished();                       // nothing left to remove: no spurious notification
        QCOMPARE(changed.size(), 1);
    }
    void unchangedProgressDoesNotNotify() {
        TaskManager m;
        const int id = m.create(spec());
        m.start(id);
        m.setProgress(id, 1, 10);
        QSignalSpy changed(&m, &TaskManager::tasksChanged);
        QVERIFY(m.setProgress(id, 1, 10));
        QVERIFY(m.setMessage(id, "a"));
        QVERIFY(m.setMessage(id, "a"));
        QCOMPARE(changed.size(), 1);
    }
    void unknownIdsAreHarmless() {
        TaskManager m;
        QVERIFY(!m.exists(42));
        QVERIFY(!m.state(42).has_value());
        QVERIFY(m.task(42).isEmpty());
        QVERIFY(!m.start(42)); QVERIFY(!m.succeed(42, "")); QVERIFY(!m.fail(42, "")); QVERIFY(!m.requestCancel(42));
    }
    void listViewForQml() {
        TaskManager m;
        TaskManager::Spec s = spec("download");
        s.total = 2048;
        const int id = m.create(s);
        const QVariantList list = m.tasks();
        QCOMPARE(list.size(), 1);
        const QVariantMap t = list.first().toMap();
        QCOMPARE(t.value("id").toInt(), id);
        QCOMPARE(t.value("kind").toString(), QString("download"));
        QCOMPARE(t.value("title").toString(), QString("report.pdf"));
        QCOMPARE(t.value("source").toString(), QString("user@host"));
        QCOMPARE(t.value("destination").toString(), QString("/home/user"));
        QCOMPARE(t.value("total").toDouble(), 2048.0);
        QVERIFY(t.value("cancellable").toBool());
        QCOMPARE(TaskManager::stateName(S::Cancelling), QString("cancelling"));
        QVERIFY(t.value("guards").toBool());                  // defaults: a task guards the session,
        QVERIFY(!t.value("session").toBool());                // is not a view,
        QVERIFY(!t.value("transient").toBool());              // and stays in the list
    }

    void beginStartsAtOnce() {
        TaskManager m;
        const int id = m.begin(spec());
        QCOMPARE(m.state(id).value(), S::Running);
    }

    // Reads that are not worth a line: success or cancellation removes them, a failure or an unknown outcome stays visible.
    void transientTasksVanishOnSuccessAndCancel() {
        TaskManager m;
        QSignalSpy finished(&m, &TaskManager::finished);
        auto readSpec = [&] { TaskManager::Spec s = spec("logsLoad"); s.transient = true; s.guards = false; s.session = true; return s; };
        const int ok = m.begin(readSpec());
        QCOMPARE(m.count(), 1);
        QVERIFY(m.succeed(ok, "done"));
        QCOMPARE(m.count(), 0);                                  // gone
        QCOMPARE(finished.size(), 1);                            // but the owner still heard about it
        QCOMPARE(finished.first().at(0).toInt(), ok);
        QVERIFY(!m.exists(ok));
        const int cancelled = m.begin(readSpec());
        QVERIFY(m.markCancelled(cancelled, "stopped"));
        QCOMPARE(m.count(), 0);
        const int failed = m.begin(readSpec());
        QVERIFY(m.fail(failed, "boom"));
        QCOMPARE(m.count(), 1);                                  // failures stay
        QCOMPARE(m.task(failed).value("state").toString(), QString("failed"));
        const int unknown = m.begin(readSpec());
        QVERIFY(m.markUnknown(unknown, "?"));
        QCOMPARE(m.count(), 2);
        QCOMPARE(m.activeCount(), 0);
        m.clearFinished();
        QCOMPARE(m.count(), 0);
        // A non-transient task is never removed by itself.
        const int kept = m.begin(spec());
        QVERIFY(m.succeed(kept, "ok"));
        QCOMPARE(m.count(), 1);
    }

    // What logout / switching server / quitting asks about: only active tasks that guard.
    void guardingTasksAreTheOnesThatMatter() {
        TaskManager m;
        QCOMPARE(m.guardingCount(), 0);
        TaskManager::Spec read = spec("logsLoad"); read.guards = false; read.session = true; read.transient = true;
        const int r = m.begin(read);
        QCOMPARE(m.activeCount(), 1);
        QCOMPARE(m.guardingCount(), 0);                          // a running read does not ask for confirmation
        const int save = m.begin(spec("docSave", false));
        const int queued = m.create(spec("upload"));             // queued work also counts: it has not run yet but will
        QCOMPARE(m.guardingCount(), 2);
        const QVariantList list = m.guardingTasks();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value("id").toInt(), save);
        QCOMPARE(list.at(1).toMap().value("id").toInt(), queued);
        QVERIFY(m.succeed(save, "saved"));
        QVERIFY(m.markCancelled(queued, "x"));
        QCOMPARE(m.guardingCount(), 0);
        QVERIFY(m.succeed(r, "done"));
    }
};
}

QTEST_GUILESS_MAIN(fortiq::TaskManagerTest)
#include "TaskManagerTest.moc"
