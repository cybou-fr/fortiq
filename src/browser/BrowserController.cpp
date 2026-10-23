#include "BrowserController.h"

#include "CurlFetch.h"
#include "HtmlSanitizer.h"
#include "UrlTools.h"
#include "../core/RemoteController.h"
#include "../core/TaskManager.h"
#include "../ssh/SshClient.h"

#include <QFutureWatcher>
#include <QJsonDocument>
#include <QLocale>
#include <QtConcurrentRun>

namespace fortiq {

struct FetchOutcome {
    SshResult run;
    QString openError;
};

namespace {
constexpr qsizetype kMaxShownText = 512 * 1024;

QString baseMime(const QString &contentType) {
    return contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 o").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 Ko").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 Mo").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

// html | text | json | image | binary
QString kindOf(const QString &mime) {
    if (mime == QLatin1String("text/html") || mime == QLatin1String("application/xhtml+xml")) return QStringLiteral("html");
    if (mime == QLatin1String("application/json") || mime.endsWith(QLatin1String("+json"))) return QStringLiteral("json");
    if (mime.startsWith(QLatin1String("text/")) || mime == QLatin1String("application/xml") || mime.endsWith(QLatin1String("+xml"))
        || mime == QLatin1String("application/javascript") || mime == QLatin1String("application/x-sh") || mime == QLatin1String("application/yaml"))
        return QStringLiteral("text");
    if (mime == QLatin1String("image/png") || mime == QLatin1String("image/jpeg") || mime == QLatin1String("image/gif")
        || mime == QLatin1String("image/webp") || mime == QLatin1String("image/bmp") || mime == QLatin1String("image/x-icon"))
        return QStringLiteral("image");
    return QStringLiteral("binary");
}

QString statusLine(int status, const QString &text) {
    return QStringLiteral("HTTP %1").arg(status) + (text.isEmpty() ? QString() : QStringLiteral(" ") + text);
}
}  // namespace

BrowserController::BrowserController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    connect(remote, &RemoteController::changed, this, &BrowserController::onRemoteChanged);
    if (remote->tasks())
        connect(remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) { if (id == m_taskId && m_cancel) m_cancel->store(true); });
}

BrowserController::~BrowserController() {
    if (m_cancel) m_cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

void BrowserController::setWindowActive(bool active) { m_active = active; }

void BrowserController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) return;
    // The server (or the whole session) changed: nothing shown may look like it came from the new one.
    if (m_cancel) m_cancel->store(true);
    ++m_seq;
    m_busy = false;
    m_token = m_remote->connected() ? m_remote->connectionToken() : -1;
    m_history.clear();
    m_index = -1;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    clearPage(m_remote->connected() ? QStringLiteral("idle") : QStringLiteral("disconnected"),
              m_remote->connected() ? QStringLiteral("Saisissez une adresse : les pages sont chargées par le serveur.")
                                    : QStringLiteral("Aucun serveur connecté"));
}

void BrowserController::clearPage(const QString &state, const QString &status) {
    m_state = state; m_status = status;
    m_url.clear(); m_title.clear(); m_kind = QStringLiteral("empty");
    m_html.clear(); m_text.clear(); m_image.clear(); m_refreshUrl.clear(); m_rawHtml.clear();
    m_headers.clear(); m_info.clear(); m_notes.clear();
    emit changed();
}

void BrowserController::open(const QString &text) {
    const UrlCheck check = normalizeUrl(text);
    if (!check.ok) {
        m_state = QStringLiteral("error");
        m_status = check.error;
        emit changed();
        return;
    }
    navigate(check.url, true);
}

void BrowserController::openLink(const QString &href) {
    const QString target = resolveLink(m_url, href);
    if (target.isEmpty()) return;   // javascript:, mailto:, #fragment ... never followed
    navigate(target, true);
}

void BrowserController::reload() {
    if (m_index >= 0 && m_index < m_history.size()) navigate(m_history.at(m_index), false);
}
void BrowserController::goBack() {
    if (!canGoBack()) return;
    --m_index;
    navigate(m_history.at(m_index), false);
}
void BrowserController::goForward() {
    if (!canGoForward()) return;
    ++m_index;
    navigate(m_history.at(m_index), false);
}
void BrowserController::stop() {
    if (m_busy && m_taskId != 0 && m_remote->tasks()) m_remote->tasks()->requestCancel(m_taskId);
    else if (m_cancel) m_cancel->store(true);
}

void BrowserController::setReader(bool on) {
    if (m_reader == on) return;
    m_reader = on;
    if (m_kind == QLatin1String("html") && !m_rawHtml.isEmpty()) {
        const SanitizedPage page = sanitizeHtml(m_rawHtml, m_url, m_reader);
        m_html = page.html;
        m_title = page.title.isEmpty() ? m_title : page.title;
    }
    emit changed();
}

void BrowserController::navigate(const QString &url, bool record) {
    if (!m_remote->connected()) {
        clearPage(QStringLiteral("disconnected"), QStringLiteral("Aucun serveur connecté"));
        return;
    }
    if (m_busy) stop();   // a new request replaces the running one; its result is ignored
    m_token = m_remote->connectionToken();
    const int token = m_token, seq = ++m_seq;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_requested = url;
    m_busy = true;
    m_state = QStringLiteral("loading");
    m_status = QStringLiteral("Chargement par le serveur…");
    m_notes.clear();

    int taskId = 0;
    if (TaskManager *tasks = m_remote->tasks()) {
        TaskManager::Spec spec;
        spec.kind = QStringLiteral("browse");
        spec.title = QUrl(url).host();
        spec.source = m_remote->serverLabel();
        spec.destination = url;
        spec.cancellable = true;
        spec.generation = token;
        taskId = m_taskId = tasks->create(spec);
        tasks->start(taskId);
    }
    emit changed();

    const ConnectionRequest request = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<FetchOutcome>(this);
    connect(watcher, &QFutureWatcher<FetchOutcome>::finished, this, [this, watcher, seq, token, url, record, taskId] {
        const FetchOutcome outcome = watcher->result();
        watcher->deleteLater();
        if (seq != m_seq || token != m_remote->connectionToken() || !m_remote->connected()) {
            if (TaskManager *tasks = m_remote->tasks()) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
            return;
        }
        apply(outcome, seq, token, url, record, taskId);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, request, url, cancel] {
        FetchOutcome outcome;
        if (cancel->load()) { outcome.run.code = QStringLiteral("cancelled"); return outcome; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { outcome.openError = opened.message; return outcome; }
        }
        outcome.run = session.runScript(buildCurlScript(url), kMaxFetchOutput, [cancel] { return !cancel->load(); });
        return outcome;
    }));
}

void BrowserController::apply(const FetchOutcome &outcome, int, int, const QString &requested, bool record, int taskId) {
    TaskManager *tasks = m_remote->tasks();
    auto fail = [&](const QString &message) {
        m_busy = false;
        m_state = QStringLiteral("error");
        m_status = message;
        m_kind = QStringLiteral("error");
        m_html.clear(); m_text.clear(); m_image.clear(); m_rawHtml.clear();
        m_info = QVariantMap { { "requested", requested } };
        m_headers.clear();
        if (tasks) tasks->fail(taskId, message);
        emit changed();
    };
    if (!outcome.openError.isEmpty()) { fail(outcome.openError); return; }
    if (outcome.run.code == QLatin1String("cancelled")) {
        m_busy = false;
        m_state = m_url.isEmpty() ? QStringLiteral("idle") : QStringLiteral("ready");
        m_status = QStringLiteral("Chargement annulé.");
        if (tasks) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
        emit changed();
        return;
    }
    if (outcome.run.status != SshResult::Status::Ok) { fail(outcome.run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : outcome.run.message); return; }
    if (outcome.run.truncated) { fail(QStringLiteral("Réponse trop grande pour être transférée (limite 5 Mio).")); return; }

    const CurlResult r = parseCurlOutput(outcome.run.data);
    if (!r.framed) {
        if (r.problem == QLatin1String("missing-curl")) fail(QStringLiteral("curl n'est pas installé sur ce serveur (sudo apt install curl)."));
        else if (r.problem == QLatin1String("mktemp")) fail(QStringLiteral("Le serveur ne peut pas créer de fichier temporaire."));
        else fail(QStringLiteral("Réponse inattendue du serveur (%1).").arg(outcome.run.code));
        return;
    }
    // curl failed before any response (DNS, connect, TLS, timeout...): an explicit error, never an empty page.
    // Exit 63 with no body is a refusal because the announced size is above the limit.
    if ((r.curlExit != 0 && r.curlExit != 63) || (r.curlExit == 63 && r.body.isEmpty())) {
        QString message = describeCurlExit(r.curlExit);
        if (!r.curlMessage.isEmpty() && r.curlExit != 28) message += QStringLiteral(" (") + r.curlMessage.section(QLatin1Char('\n'), 0, 0).left(200) + QLatin1Char(')');
        fail(message);
        return;
    }

    // ---- a page was received ----
    m_busy = false;
    m_url = r.effectiveUrl.isEmpty() ? requested : r.effectiveUrl;
    m_headers = r.headers;
    m_notes.clear();
    m_refreshUrl.clear();
    m_html.clear(); m_text.clear(); m_image.clear(); m_rawHtml.clear(); m_title.clear();
    const QString mime = baseMime(r.contentType);
    m_kind = r.body.isEmpty() ? QStringLiteral("empty") : kindOf(mime);
    QString charset;
    if (m_kind == QLatin1String("html")) {
        const DecodedText decoded = decodeBody(r.body, r.contentType, true);
        charset = decoded.charset;
        if (!decoded.note.isEmpty()) m_notes << decoded.note;
        m_rawHtml = decoded.text;
        const SanitizedPage page = sanitizeHtml(m_rawHtml, m_url, m_reader);
        m_html = page.html;
        m_title = page.title;
        m_refreshUrl = page.refreshUrl;
        if (page.readerExtracted) m_notes << QStringLiteral("Mode lecture : seul le contenu principal est affiché.");
        if (page.scriptCount > 0 && page.plainText.size() < 300)
            m_notes << QStringLiteral("Cette page dépend de JavaScript, qui n'est jamais exécuté : le contenu affiché est limité.");
        if (page.truncated) m_notes << QStringLiteral("Page très longue : affichage tronqué.");
        if (!m_refreshUrl.isEmpty()) m_notes << QStringLiteral("La page demande une redirection vers %1 (non suivie automatiquement).").arg(m_refreshUrl);
        if (page.imageCount > 0) m_notes << QStringLiteral("%1 image(s) non chargée(s) : le texte alternatif est affiché.").arg(page.imageCount);
    } else if (m_kind == QLatin1String("text") || m_kind == QLatin1String("json")) {
        const DecodedText decoded = decodeBody(r.body, r.contentType, false);
        charset = decoded.charset;
        if (!decoded.note.isEmpty()) m_notes << decoded.note;
        QString text = decoded.text;
        if (m_kind == QLatin1String("json")) {
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(r.body, &error);
            if (error.error == QJsonParseError::NoError) text = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        }
        if (text.size() > kMaxShownText) { text.truncate(kMaxShownText); m_notes << QStringLiteral("Texte long : les 512 premiers Kio sont affichés."); }
        m_text = text;
        m_title = m_url;
    } else if (m_kind == QLatin1String("image")) {
        m_image = QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(r.body.toBase64()));
        m_title = m_url;
    } else if (m_kind == QLatin1String("binary")) {
        m_title = m_url;
        m_notes << QStringLiteral("Contenu « %1 » non affichable ici (%2).").arg(mime.isEmpty() ? QStringLiteral("type inconnu") : mime, humanSize(r.sizeDownload));
    } else {
        m_title = m_url;
    }
    if (r.curlExit == 63 || r.truncated) m_notes << QStringLiteral("Réponse tronquée à 5 Mio.");
    if (QUrl(m_url).scheme() == QLatin1String("http")) m_notes << QStringLiteral("Connexion non chiffrée (http) entre le serveur et le site.");
    if (r.redirects > 0) m_notes << QStringLiteral("%1 redirection(s) suivie(s) par le serveur.").arg(r.redirects);
    if (r.status >= 400) m_notes << QStringLiteral("Le site a répondu par une erreur (%1).").arg(statusLine(r.status, r.statusText));

    QVariantMap timings {
        { "dns", r.timeDns }, { "connect", r.timeConnect }, { "tls", r.timeTls }, { "firstByte", r.timeFirstByte }, { "total", r.timeTotal } };
    m_info = QVariantMap {
        { "requested", requested }, { "status", r.status }, { "statusText", r.statusText }, { "httpVersion", r.httpVersion },
        { "remoteIp", r.remoteIp }, { "contentType", r.contentType }, { "charset", charset }, { "size", r.sizeDownload },
        { "redirects", r.redirects }, { "redirectChain", r.redirectChain }, { "tlsVerified", r.tlsVerify == 0 }, { "timings", timings },
        { "truncated", r.truncated || r.curlExit == 63 } };

    if (record) {
        if (m_index + 1 < m_history.size()) m_history = m_history.mid(0, m_index + 1);
        if (m_history.isEmpty() || m_history.last() != requested) { m_history << requested; }
        m_index = static_cast<int>(m_history.size()) - 1;
    }
    m_state = QStringLiteral("ready");
    m_status = statusLine(r.status, r.statusText) + QStringLiteral(" · ") + humanSize(r.sizeDownload)
        + QStringLiteral(" · ") + QLocale().toString(r.timeTotal, 'f', 2) + QStringLiteral(" s");
    if (tasks) tasks->succeed(taskId, m_status);
    emit changed();
}

}  // namespace fortiq
