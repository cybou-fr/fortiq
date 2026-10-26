// Scene-cost baseline of the Files window (docs/PERFORMANCE_BASELINE.md): loads the REAL FilesWindow.qml with a stand-in controller that
// serves N entries through the same property types as RemoteController (QVariantList -> JS array), and measures what the user waits for:
//   build   the window shown with N entries (first frame included)
//   change  an unrelated controller change (busy flips) re-evaluating the bindings that read `entries`
//   scroll  frame time while the view scrolls
// Run: fortiq_perf_scene [N ...]  (default 1000 5000 20000 50000), grid and list view. Never part of ctest.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstdio>

namespace {
class FakeFiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString currentDirectory READ currentDirectory CONSTANT)
    Q_PROPERTY(QString homeDirectory READ homeDirectory CONSTANT)
    Q_PROPERTY(QString trashDirectory READ trashDirectory CONSTANT)
    Q_PROPERTY(QString serverLabel READ serverLabel CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)   // as RemoteController: its own signal
    Q_PROPERTY(int totalEntries READ totalEntries NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool listingCut READ listingCut NOTIFY changed)
    Q_PROPERTY(int lastLatencyMs READ lastLatencyMs NOTIFY changed)
public:
    explicit FakeFiles(int n) {
        for (int i = 0; i < n; ++i) {
            const bool dir = i % 17 == 0;
            const QString name = (dir ? QStringLiteral("dossier-") : QStringLiteral("Fichier-")) + QString::number(i) + (dir ? QString() : QStringLiteral(".txt"));
            m_entries.push_back(QVariantMap { { "name", name }, { "directory", dir }, { "link", false }, { "hidden", false },
                { "path", QStringLiteral("/home/demo/") + name }, { "size", double(i) * 1000 }, { "mtime", 1790000000.0 + i },
                { "mode", QStringLiteral("-rw-r--r--") }, { "owner", QStringLiteral("debian") } });
        }
    }
    bool connected() const { return true; }
    bool busy() const { return m_busy; }
    QString currentDirectory() const { return QStringLiteral("/home/demo"); }
    QString homeDirectory() const { return QStringLiteral("/home/demo"); }
    QString trashDirectory() const { return QStringLiteral("/home/demo/.local/share/Trash/files"); }
    QString serverLabel() const { return QStringLiteral("demo@server"); }
    QString statusText() const { return QString(); }
    QVariantList entries() const { return m_entries; }
    int totalEntries() const { return int(m_entries.size()); }
    bool hasMore() const { return false; }
    bool listingCut() const { return false; }
    int lastLatencyMs() const { return 5; }
    void setBusy(bool b) { m_busy = b; emit changed(); }
    Q_INVOKABLE void loadMore(bool) {}
    Q_INVOKABLE void navigate(const QString &) {}
signals:
    void changed();
    void entriesChanged();
private:
    QVariantList m_entries;
    bool m_busy = false;
};

void settle(QQuickWindow *w) {
    for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
    w->grabWindow();   // forces a full sync + render
}
}  // namespace

int main(int argc, char **argv) {
    qputenv("QT_QUICK_BACKEND", "software");   // the layout tests' backend: no GPU needed, so the figures are CPU-only and comparable between machines
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QGuiApplication app(argc, argv);
    QList<int> sizes;
    for (int i = 1; i < argc; ++i) sizes << QString::fromLocal8Bit(argv[i]).toInt();
    if (sizes.isEmpty()) sizes = { 1000, 5000, 20000, 50000 };
    std::printf("| entries | view | build (ms) | unrelated change (ms) | scroll frame median (ms) | scroll frame max (ms) |\n|---|---|---|---|---|---|\n");
    for (const int n : sizes) {
        for (const char *view : { "grid", "list" }) {
            QQmlApplicationEngine engine;
            engine.addImportPath(QStringLiteral("qml"));
            FakeFiles files(n);
            QQmlComponent component(&engine);
            component.setData(QByteArrayLiteral("import QtQuick\nimport QtQuick.Window\nimport Fortiq.Shell\nWindow { width: 900; height: 600; visible: true; color: '#0b1220'\n"
                "  property alias files: f\n  FilesWindow { id: f; width: 900; height: 600; controller: null; viewMode: \"") + view + QByteArrayLiteral("\" }\n}"), QUrl());
            auto *window = qobject_cast<QQuickWindow *>(component.create());
            if (!window) { std::printf("| %d | %s | FAILED: %s |\n", n, view, qPrintable(component.errorString())); continue; }
            settle(window);
            auto *fw = window->findChild<QQuickItem *>("filesWindow");
            if (!fw) { std::printf("| %d | %s | FAILED: no filesWindow |\n", n, view); delete window; continue; }
            QElapsedTimer t;
            t.start();
            fw->setProperty("controller", QVariant::fromValue<QObject *>(&files));
            settle(window);
            const qint64 build = t.elapsed();
            t.restart();
            files.setBusy(true);
            settle(window);
            files.setBusy(false);
            settle(window);
            const double change = t.elapsed() / 2.0;
            // scroll: move the view's contentY in steps and time each frame
            QQuickItem *list = fw->property("view").value<QQuickItem *>();
            std::vector<double> frames;
            if (list) {
                const double height = list->property("contentHeight").toDouble();
                for (int i = 0; i < 40; ++i) {
                    list->setProperty("contentY", height * i / 40.0);
                    QElapsedTimer ft; ft.start();
                    settle(window);
                    frames.push_back(double(ft.nsecsElapsed()) / 1e6);
                }
                std::sort(frames.begin(), frames.end());
            }
            std::printf("| %d | %s | %lld | %.0f | %.1f | %.1f |\n", n, view, build, change, frames.empty() ? -1.0 : frames[frames.size() / 2], frames.empty() ? -1.0 : frames.back());
            std::fflush(stdout);
            fw->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
            delete window;
        }
    }
    return 0;
}

#include "PerfFilesScene.moc"
