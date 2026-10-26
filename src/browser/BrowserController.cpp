#include "BrowserController.h"

#include "CurlFetch.h"
#include "HtmlSanitizer.h"
#include "UrlPolicy.h"
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
    enum class Kind { Done, Confirm, Error } kind = Kind::Done;
    SshResult run;               // the last curl run (or the resolve run that failed)
    QString openError;
    CurlResult curl;             // the parsed last response (Done)
    QString error;               // Error: user-facing text
    bool blocked = false;        // Error caused by the address policy (no way to confirm)
    // Confirm: what the user is asked about
    PolicyDecision decision;
    QString confirmUrl, confirmHost, fromUrl;
    int confirmPort = 0, hop = 0;
    // Done: where the page really came from
    QVariantList chain;          // redirects followed: [{status, from, location}]
    QString pinnedIp, finalUrl, addressLabel;
    // Done, HTML page: decoded and sanitized on the worker thread (a 3 MB page takes ~2.6 s; the window must not freeze meanwhile).
    // apply() uses it only when the base address and reader mode are still the ones it was made for.
    bool hasPage = false;
    DecodedText decoded;
    SanitizedPage page;
    QString pageBase;
    bool pageReader = true;
};

struct BrowserController::Chain {
    QString url;                 // the address to fetch next
    QString requested;           // what the user asked for (the history key)
    bool record = false;
    int hop = 0, seq = 0, token = 0, taskId = 0;
    QStringList confirmed;       // "host:port" the user allowed, for THIS navigation only
    QVariantList chain;          // redirects followed so far
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
    m_pending.reset(); m_pendingMap.clear();
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
    if (m_pending) { cancelConfirm(); return; }
    if (m_busy && m_taskId != 0 && m_remote->tasks()) m_remote->tasks()->requestCancel(m_taskId);
    else if (m_cancel) m_cancel->store(true);
}

void BrowserController::setReader(bool on) {
    if (m_reader == on) return;
    m_reader = on;
    emit changed();
    if (m_kind == QLatin1String("html") && !m_rawHtml.isEmpty()) {
        // Sanitizing can take seconds on a big page: do it on a worker; the view switches when it is ready.
        const int generation = ++m_readerGeneration, seq = m_seq;
        auto *watcher = new QFutureWatcher<SanitizedPage>(this);
        connect(watcher, &QFutureWatcher<SanitizedPage>::finished, this, [this, watcher, generation, seq] {
            const SanitizedPage page = watcher->result();
            watcher->deleteLater();
            if (generation != m_readerGeneration || seq != m_seq || m_kind != QLatin1String("html")) return;   // a newer toggle or page won
            m_html = page.html;
            m_title = page.title.isEmpty() ? m_title : page.title;
            emit changed();
        });
        const QString raw = m_rawHtml, url = m_url;
        const bool reader = m_reader;
        watcher->setFuture(QtConcurrent::run([raw, url, reader] { return sanitizeHtml(raw, url, reader); }));
    }
}

void BrowserController::navigate(const QString &url, bool record) {
    if (!m_remote->connected()) {
        clearPage(QStringLiteral("disconnected"), QStringLiteral("Aucun serveur connecté"));
        return;
    }
    if (m_pending) {   // a new request replaces an unanswered question
        if (TaskManager *tasks = m_remote->tasks()) tasks->markCancelled(m_pending->taskId, QStringLiteral("Remplacé par une nouvelle adresse."));
        m_pending.reset(); m_pendingMap.clear();
    }
    if (m_busy) stop();   // a new request replaces the running one; its result is ignored
    m_token = m_remote->connectionToken();
    const int token = m_token, seq = ++m_seq;
    m_cancel = std::make_shared<std::atomic_bool>(false);
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
        spec.guards = false; spec.session = true;   // a page fetch: leaving loses nothing
        spec.generation = token;
        taskId = m_taskId = tasks->create(spec);
        tasks->start(taskId);
    }
    emit changed();

    Chain chain;
    chain.url = url; chain.requested = url; chain.record = record; chain.seq = seq; chain.token = token; chain.taskId = taskId;
    runChain(chain);
}

// One navigation = a loop of hops run on the worker thread: for every address (the typed one and each redirect target) the
// SERVER resolves the host, the client classifies what came back, and only an approved address is fetched, pinned with
// --resolve so that curl cannot connect anywhere else. curl never follows a redirect itself.
void BrowserController::runChain(const Chain &chain) {
    auto cancel = m_cancel;
    const bool reader = m_reader;
    const ConnectionRequest request = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<FetchOutcome>(this);
    connect(watcher, &QFutureWatcher<FetchOutcome>::finished, this, [this, watcher, chain] {
        const FetchOutcome outcome = watcher->result();
        watcher->deleteLater();
        if (chain.seq != m_seq || chain.token != m_remote->connectionToken() || !m_remote->connected()) {
            if (TaskManager *tasks = m_remote->tasks()) tasks->markCancelled(chain.taskId, QStringLiteral("Annulé."));
            return;
        }
        onChain(outcome, chain);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, request, chain, cancel, reader] {
        FetchOutcome out;
        out.chain = chain.chain;
        auto cancelled = [&] { return cancel->load(); };
        if (cancelled()) { out.run.code = QStringLiteral("cancelled"); return out; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { out.openError = opened.message; return out; }
        }
        QString url = chain.url;
        int hop = chain.hop;
        QVariantList followed = chain.chain;
        for (;;) {
            if (cancelled()) { out.run.code = QStringLiteral("cancelled"); return out; }
            const UrlCheck check = normalizeUrl(url);
            if (!check.ok) { out.kind = FetchOutcome::Kind::Error; out.error = check.error; return out; }
            const HostPlan plan = planHost(check.url);
            if (!plan.ok) { out.kind = FetchOutcome::Kind::Error; out.error = plan.error; return out; }
            QStringList ips;
            if (plan.literalIp) ips << plan.ip;
            else {
                const SshResult resolved = session.runScript(buildResolveScript(plan.host), 4096, [cancel] { return !cancel->load(); });
                if (resolved.code == QLatin1String("cancelled")) { out.run = resolved; return out; }
                if (resolved.status != SshResult::Status::Ok) { out.run = resolved; return out; }
                QString problem;
                ips = parseResolveOutput(resolved.data, &problem);
                if (!problem.isEmpty()) {
                    out.kind = FetchOutcome::Kind::Error;
                    out.error = problem == QLatin1String("missing-getent") ? QStringLiteral("getent n'est pas disponible sur ce serveur : la vérification des adresses est impossible, rien n'est ouvert.") : problem;
                    return out;
                }
                if (ips.isEmpty()) { out.kind = FetchOutcome::Kind::Error; out.error = QStringLiteral("Nom d'hôte introuvable depuis le serveur (DNS)."); return out; }
            }
            const PolicyDecision decision = decideAddresses(ips);
            const QString hostKey = QStringLiteral("%1:%2").arg(plan.host).arg(plan.port);
            if (decision.kind == PolicyDecision::Kind::Block) { out.kind = FetchOutcome::Kind::Error; out.blocked = true; out.error = decision.reason; return out; }
            if (decision.kind == PolicyDecision::Kind::Confirm && !chain.confirmed.contains(hostKey)) {
                out.kind = FetchOutcome::Kind::Confirm;
                out.decision = decision; out.confirmUrl = check.url; out.confirmHost = plan.host; out.confirmPort = plan.port; out.hop = hop; out.chain = followed;
                out.fromUrl = followed.isEmpty() ? QString() : followed.last().toMap().value("from").toString();
                return out;
            }
            const QString spec = plan.literalIp ? QString() : curlResolveSpec(plan.host, plan.port, decision.pinIp);
            const SshResult run = session.runScript(buildCurlScript(check.url, spec), kMaxFetchOutput, [cancel] { return !cancel->load(); });
            out.run = run;
            out.pinnedIp = plan.literalIp ? plan.ip : decision.pinIp;
            out.finalUrl = check.url;
            out.addressLabel = classifyAddress(out.pinnedIp).label;
            if (run.code == QLatin1String("cancelled") || run.status != SshResult::Status::Ok || run.truncated) return out;
            out.curl = parseCurlOutput(run.data);
            if (!out.curl.framed || (out.curl.curlExit != 0 && out.curl.curlExit != 63)) return out;
            const QString location = redirectLocation(out.curl);
            if (location.isEmpty()) {
                if (!out.curl.body.isEmpty() && kindOf(baseMime(out.curl.contentType)) == QLatin1String("html")) {
                    out.pageBase = out.finalUrl.isEmpty() ? (out.curl.effectiveUrl.isEmpty() ? chain.requested : out.curl.effectiveUrl) : out.finalUrl;
                    out.pageReader = reader;
                    out.decoded = decodeBody(out.curl.body, out.curl.contentType, true);
                    out.page = sanitizeHtml(out.decoded.text, out.pageBase, reader);
                    out.hasPage = true;
                }
                return out;
            }
            if (followed.size() >= 10) { out.kind = FetchOutcome::Kind::Error; out.error = describeCurlExit(47); return out; }
            const QString next = resolveLink(check.url, location);
            if (next.isEmpty()) {
                out.kind = FetchOutcome::Kind::Error;
                out.error = QStringLiteral("La page redirige vers une adresse non autorisée (« %1 ») : seules les adresses http et https sont suivies.").arg(location.left(120));
                return out;
            }
            followed.push_back(QVariantMap { { "status", out.curl.status }, { "from", check.url }, { "location", next } });
            out.chain = followed;
            url = next;
            ++hop;
            out.curl = CurlResult();
        }
    }));
}

void BrowserController::onChain(const FetchOutcome &outcome, const Chain &chain) {
    TaskManager *tasks = m_remote->tasks();
    if (outcome.kind == FetchOutcome::Kind::Confirm) {
        // Nothing has been fetched from this address. The task stays open (running) while the question is up.
        m_busy = false;
        m_state = QStringLiteral("confirm");
        m_status = QStringLiteral("Adresse interne : confirmation requise.");
        m_pending = std::make_shared<Chain>(chain);
        m_pending->url = outcome.confirmUrl;
        m_pending->hop = outcome.hop;
        m_pending->chain = outcome.chain;
        m_pendingMap = QVariantMap { { "url", outcome.confirmUrl }, { "host", outcome.confirmHost }, { "port", outcome.confirmPort }, { "reason", outcome.decision.reason },
                                     { "details", outcome.decision.details }, { "hop", outcome.hop }, { "from", outcome.fromUrl } };
        if (tasks) tasks->setMessage(chain.taskId, QStringLiteral("En attente de votre confirmation (adresse interne)."));
        emit changed();
        return;
    }
    if (outcome.kind == FetchOutcome::Kind::Error) {
        m_busy = false;
        m_state = QStringLiteral("error");
        m_status = outcome.error;
        m_kind = QStringLiteral("error");
        m_html.clear(); m_text.clear(); m_image.clear(); m_rawHtml.clear(); m_headers.clear();
        m_info = QVariantMap { { "requested", chain.requested }, { "blocked", outcome.blocked } };
        if (tasks) tasks->fail(chain.taskId, outcome.error, outcome.blocked ? QStringLiteral("blocked") : QString());
        emit changed();
        return;
    }
    apply(outcome, chain.seq, chain.token, chain.requested, chain.record, chain.taskId);
}

void BrowserController::confirmInternal() {
    if (!m_pending || m_state != QLatin1String("confirm")) return;
    Chain chain = *m_pending;
    m_pending.reset();
    m_pendingMap.clear();
    chain.confirmed << QStringLiteral("%1:%2").arg(QUrl(chain.url).host(), QString::number(QUrl(chain.url).port(QUrl(chain.url).scheme() == QLatin1String("https") ? 443 : 80)));
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_busy = true;
    m_state = QStringLiteral("loading");
    m_status = QStringLiteral("Chargement par le serveur…");
    if (TaskManager *tasks = m_remote->tasks()) tasks->setMessage(chain.taskId, QString());
    emit changed();
    runChain(chain);
}

void BrowserController::cancelConfirm() {
    if (!m_pending) return;
    const int taskId = m_pending->taskId;
    m_pending.reset();
    m_pendingMap.clear();
    m_busy = false;
    m_state = m_url.isEmpty() ? QStringLiteral("idle") : QStringLiteral("ready");
    m_status = QStringLiteral("Adresse interne : ouverture annulée.");
    if (TaskManager *tasks = m_remote->tasks()) tasks->markCancelled(taskId, QStringLiteral("Annulé : adresse interne non confirmée."));
    emit changed();
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

    CurlResult r = outcome.curl;
    r.redirects = static_cast<int>(outcome.chain.size());
    r.redirectChain = outcome.chain;
    if (!outcome.finalUrl.isEmpty()) r.effectiveUrl = outcome.finalUrl;
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
        const bool prepared = outcome.hasPage && outcome.pageReader == m_reader && outcome.pageBase == m_url;
        const DecodedText decoded = prepared ? outcome.decoded : decodeBody(r.body, r.contentType, true);
        charset = decoded.charset;
        if (!decoded.note.isEmpty()) m_notes << decoded.note;
        m_rawHtml = decoded.text;
        const SanitizedPage page = prepared ? outcome.page : sanitizeHtml(m_rawHtml, m_url, m_reader);
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
        { "truncated", r.truncated || r.curlExit == 63 }, { "pinnedIp", outcome.pinnedIp }, { "addressClass", outcome.addressLabel } };

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
