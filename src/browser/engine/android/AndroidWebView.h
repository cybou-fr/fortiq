#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace fortiq::engine {

// One page of the Browser engine on Android: a native android.webkit.WebView (Chromium, system component) laid over this item and driven
// through android/src/fr/cybou/fortiq/FortiqWeb.java. Same surface as qml/engine/EngineView.qml (url, loading, title, goBack(), ...),
// so qml/engine/EngineWindow.qml is shared with the desktop. The rules (proxy plan, URL judgement, credentials) are EngineProfile.cpp's.
//   - Nothing is created unless the WebView confirms the proxy override: `failure` then carries the reason and the page never loads.
//   - A native view cannot be drawn under QML: `suspended` hides it while QML must show something above the page (the address question).
class AndroidWebView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *engine READ engine WRITE setEngine NOTIFY engineChanged REQUIRED)
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(QString failure READ failure NOTIFY failureChanged)
    Q_PROPERTY(bool certificateProblem READ certificateProblem NOTIFY failureChanged)
    Q_PROPERTY(bool suspended READ suspended WRITE setSuspended NOTIFY suspendedChanged)
    Q_PROPERTY(bool started READ started NOTIFY startedChanged)     // the WebView exists (proxy confirmed)
public:
    explicit AndroidWebView(QQuickItem *parent = nullptr);
    ~AndroidWebView() override;

    QObject *engine() const { return m_engine.data(); }
    void setEngine(QObject *engine);
    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);
    bool loading() const { return m_loading; }
    int loadProgress() const { return m_progress; }
    QString title() const { return m_title; }
    bool canGoBack() const { return m_back; }
    bool canGoForward() const { return m_forward; }
    QString failure() const { return m_failure; }
    bool certificateProblem() const { return m_certificate; }
    bool suspended() const { return m_suspended; }
    void setSuspended(bool s);
    bool started() const { return m_started; }

    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();

    // Called from the Java side (queued onto this object's thread).
    void handleEvent(int kind, const QString &a, const QString &b, int n);

signals:
    void engineChanged();
    void urlChanged();
    void loadingChanged();
    void loadProgressChanged();
    void titleChanged();
    void historyChanged();
    void failureChanged();
    void suspendedChanged();
    void startedChanged();
    void refused(const QString &text);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void begin();
    void place();
    void fail(const QString &text);

    QPointer<QObject> m_engine;
    QUrl m_url, m_pending;
    bool m_loading = false, m_back = false, m_forward = false, m_certificate = false, m_suspended = false, m_started = false;
    bool m_completed = false, m_beginCalled = false;
    int m_progress = 0;
    QString m_title, m_failure;
    QTimer m_placeTimer;
    QRect m_lastPlace;
    bool m_lastVisible = false;
};

}  // namespace fortiq::engine
