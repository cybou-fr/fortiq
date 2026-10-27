#include "AndroidWebView.h"

#include "../EngineBrowser.h"
#include "../EngineProfile.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>

namespace fortiq::engine {

namespace {
constexpr const char *kClass = "fr/cybou/fortiq/FortiqWeb";
// The WebView lives in the Java UI thread: its callbacks reach the current item and the credentials of the current gateway session
// through these guarded globals (never through a pointer the Qt thread could be destroying).
QMutex g_lock;
QPointer<AndroidWebView> g_current;
struct Credentials { QString user, password, realm; } g_credentials;
bool g_registered = false;

jstring toJava(JNIEnv *env, const QString &text) { return env->NewString(reinterpret_cast<const jchar *>(text.utf16()), jsize(text.size())); }
QString fromJava(JNIEnv *env, jstring s) {
    if (!s) return {};
    const jchar *chars = env->GetStringChars(s, nullptr);
    const QString out = QString(reinterpret_cast<const QChar *>(chars), qsizetype(env->GetStringLength(s)));
    env->ReleaseStringChars(s, chars);
    return out;
}

jstring nativeJudge(JNIEnv *env, jclass, jstring url, jboolean topLevel) {
    const NavigationVerdict v = judgeUrl(QUrl(fromJava(env, url)), topLevel);
    return toJava(env, v.allowed ? QString() : v.reason);
}

jobjectArray nativeCredentials(JNIEnv *env, jclass, jstring host, jstring realm) {
    Credentials c;
    { QMutexLocker l(&g_lock); c = g_credentials; }
    if (c.password.isEmpty() || !acceptProxyChallenge(fromJava(env, host), fromJava(env, realm), c.realm)) return nullptr;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray out = env->NewObjectArray(2, stringClass, nullptr);
    env->SetObjectArrayElement(out, 0, toJava(env, c.user));
    env->SetObjectArrayElement(out, 1, toJava(env, c.password));
    return out;
}

void nativeEvent(JNIEnv *env, jclass, jint kind, jstring a, jstring b, jint n) {
    const QString sa = fromJava(env, a), sb = fromJava(env, b);
    QPointer<AndroidWebView> target;
    { QMutexLocker l(&g_lock); target = g_current; }
    if (!target) return;
    QMetaObject::invokeMethod(target.data(), [target, kind, sa, sb, n] { if (target) target->handleEvent(kind, sa, sb, n); }, Qt::QueuedConnection);
}

bool registerNatives() {
    if (g_registered) return true;
    static const JNINativeMethod methods[] = {
        { "nativeJudge", "(Ljava/lang/String;Z)Ljava/lang/String;", reinterpret_cast<void *>(nativeJudge) },
        { "nativeCredentials", "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;", reinterpret_cast<void *>(nativeCredentials) },
        { "nativeEvent", "(ILjava/lang/String;Ljava/lang/String;I)V", reinterpret_cast<void *>(nativeEvent) },
    };
    QJniEnvironment env;
    g_registered = env.registerNativeMethods(kClass, methods, 3);
    return g_registered;
}

// Events (FortiqWeb.EV_*).
enum { EvReady = 1, EvFailed, EvStarted, EvFinished, EvProgress, EvTitle, EvUrl, EvError, EvRefused, EvHistory, EvDownload, EvCert };
}  // namespace

AndroidWebView::AndroidWebView(QQuickItem *parent) : QQuickItem(parent) {
    m_placeTimer.setInterval(100);
    connect(&m_placeTimer, &QTimer::timeout, this, &AndroidWebView::place);
}

AndroidWebView::~AndroidWebView() {
    m_placeTimer.stop();
    {
        QMutexLocker l(&g_lock);
        if (g_current == this) { g_current = nullptr; g_credentials = {}; }
    }
    // The view and everything it stored go; the proxy override stays (a later view can only reach the gateway or nothing).
    if (m_beginCalled) QJniObject::callStaticMethod<void>(kClass, "destroy", "()V");
}

void AndroidWebView::setEngine(QObject *engine) {
    if (m_engine == engine) return;
    m_engine = engine;
    emit engineChanged();
    if (m_completed) begin();
}

void AndroidWebView::componentComplete() {
    QQuickItem::componentComplete();
    m_completed = true;
    begin();
}

void AndroidWebView::fail(const QString &text) {
    m_failure = text;
    m_started = false;
    emit failureChanged();
    emit startedChanged();
}

void AndroidWebView::begin() {
    if (m_beginCalled) return;
    auto *eb = qobject_cast<EngineBrowser *>(m_engine.data());
    if (!eb) return;
    const quint16 port = eb->proxyPort();
    if (eb->state() != QLatin1String("ready") || port == 0) { fail(QStringLiteral("La passerelle SSH n'est pas active : le navigateur ne démarre pas.")); return; }
    const AndroidProxyPlan plan = androidProxyPlan(port);
    const QStringList problems = auditAndroidProxyPlan(plan, port);
    if (!problems.isEmpty()) { fail(problems.join(QStringLiteral("; "))); return; }
    const QVariantMap c = eb->proxyCredentials(QStringLiteral("127.0.0.1"), port);
    if (c.isEmpty()) { fail(QStringLiteral("Identifiants de la passerelle indisponibles.")); return; }
    if (!registerNatives()) { fail(QStringLiteral("Le pont Java du navigateur n'a pas pu être enregistré.")); return; }
    {
        QMutexLocker l(&g_lock);
        g_current = this;
        g_credentials = { c.value(QStringLiteral("user")).toString(), c.value(QStringLiteral("password")).toString(), eb->proxyRealm() };
    }
    m_beginCalled = true;
    QJniEnvironment env;
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray bypass = env->NewObjectArray(jsize(plan.bypass.size()), stringClass, nullptr);
    for (int i = 0; i < plan.bypass.size(); ++i) env->SetObjectArrayElement(bypass, i, toJava(env.jniEnv(), plan.bypass.at(i)));
    QJniObject::callStaticMethod<void>(kClass, "start",
        "(Landroid/app/Activity;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;J)V",
        activity.object(), toJava(env.jniEnv(), plan.rule), bypass, toJava(env.jniEnv(), androidWebRtcRemovalScript()), jlong(15000));
}

void AndroidWebView::setUrl(const QUrl &url) {
    if (url.isEmpty()) return;
    auto *eb = qobject_cast<EngineBrowser *>(m_engine.data());
    if (eb) {
        const QVariantMap verdict = eb->judge(url.toString(), true);
        if (!verdict.value(QStringLiteral("allowed")).toBool()) { emit refused(verdict.value(QStringLiteral("reason")).toString()); return; }
    }
    if (!m_started) { m_pending = url; return; }
    QJniObject::callStaticMethod<void>(kClass, "load", "(Ljava/lang/String;)V", QJniObject::fromString(url.toString()).object<jstring>());
}

void AndroidWebView::goBack() { if (m_started) QJniObject::callStaticMethod<void>(kClass, "back", "()V"); }
void AndroidWebView::goForward() { if (m_started) QJniObject::callStaticMethod<void>(kClass, "forward", "()V"); }
void AndroidWebView::reload() { if (m_started) QJniObject::callStaticMethod<void>(kClass, "reload", "()V"); }
void AndroidWebView::stop() { if (m_started) QJniObject::callStaticMethod<void>(kClass, "stop", "()V"); }

void AndroidWebView::setSuspended(bool s) {
    if (m_suspended == s) return;
    m_suspended = s;
    emit suspendedChanged();
    place();
}

void AndroidWebView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    place();
}

void AndroidWebView::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) place();
}

void AndroidWebView::place() {
    if (!m_started || !window()) return;
    const qreal dpr = window()->effectiveDevicePixelRatio();
    const QPointF origin = mapToGlobal(QPointF(0, 0));
    const QRect r(qRound(origin.x() * dpr), qRound(origin.y() * dpr), qRound(width() * dpr), qRound(height() * dpr));
    const bool visible = isVisible() && !m_suspended && window()->isVisible() && width() > 1 && height() > 1 && opacity() > 0;
    if (r == m_lastPlace && visible == m_lastVisible) return;
    m_lastPlace = r;
    m_lastVisible = visible;
    QJniObject::callStaticMethod<void>(kClass, "place", "(IIIIZ)V", jint(r.x()), jint(r.y()), jint(r.width()), jint(r.height()), jboolean(visible));
}

void AndroidWebView::handleEvent(int kind, const QString &a, const QString &b, int n) {
    auto *eb = qobject_cast<EngineBrowser *>(m_engine.data());
    switch (kind) {
    case EvReady:
        m_started = true;
        m_failure.clear();
        emit startedChanged();
        emit failureChanged();
        m_placeTimer.start();
        m_lastPlace = QRect();
        place();
        if (!m_pending.isEmpty()) { const QUrl u = m_pending; m_pending.clear(); setUrl(u); }
        break;
    case EvFailed: {
        QString text = a;
        if (a.startsWith(QLatin1String("support:"))) {
            const QStringList f = a.split(QLatin1Char(':'));
            text = androidSupportRefusal(f.value(1) == QLatin1String("true"), f.value(2) == QLatin1String("true"), f.value(3) == QLatin1String("true"));
        }
        fail(text);
        break;
    }
    case EvStarted:
        m_failure.clear();
        m_certificate = false;
        m_loading = true;
        if (!a.isEmpty()) m_url = QUrl(a);
        emit failureChanged();
        emit loadingChanged();
        emit urlChanged();
        break;
    case EvFinished:
        m_loading = false;
        if (!a.isEmpty()) m_url = QUrl(a);
        if (!b.isEmpty()) { m_title = b; emit titleChanged(); }
        emit loadingChanged();
        emit urlChanged();
        break;
    case EvProgress:
        m_progress = n;
        emit loadProgressChanged();
        break;
    case EvTitle:
        m_title = a;
        emit titleChanged();
        break;
    case EvUrl:
        if (!a.isEmpty() && QUrl(a) != m_url) { m_url = QUrl(a); emit urlChanged(); }
        break;
    case EvError:
        m_failure = b == QLatin1String("render-gone") ? a : QStringLiteral("%1 (%2)").arg(a, b);
        m_loading = false;
        emit failureChanged();
        emit loadingChanged();
        break;
    case EvRefused:
        emit refused(b == QLatin1String("permission") && eb ? eb->permissionRefusal() : a);
        break;
    case EvHistory:
        m_back = n & 1;
        m_forward = n & 2;
        emit historyChanged();
        break;
    case EvDownload:
        if (eb) eb->reportDownloadRefused(a);
        break;
    case EvCert:
        m_certificate = true;
        emit failureChanged();
        emit refused(a);
        break;
    }
}

}  // namespace fortiq::engine
