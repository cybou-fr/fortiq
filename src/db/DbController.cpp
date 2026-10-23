#include "DbController.h"

#include "../core/RemoteController.h"
#include "../core/SecretStore.h"
#include "../core/TaskManager.h"
#include "../ssh/SshClient.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QtConcurrentRun>

namespace fortiq {

namespace {
struct Outcome {
    SshResult run;
    QString openError;
    DbResult result;
    QList<DbObject> objects;
    RoleCheck role;
    bool cancelled = false;
};

QString secretKey(const QString &id) { return QStringLiteral("db-") + id; }

QJsonObject toJson(const DbProfile &p) {
    return QJsonObject { { "id", p.id }, { "serverId", p.serverId }, { "name", p.name }, { "engine", engineName(p.engine) }, { "path", p.path },
                         { "host", p.host }, { "port", p.port }, { "user", p.user }, { "database", p.database }, { "hasPassword", p.hasPassword } };
}

int exitCodeOf(const QString &code) {
    return code.startsWith(QLatin1String("exit:")) ? code.mid(5).toInt() : -1;
}
}  // namespace

DbController::DbController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    QSettings s;
    m_limits = clampLimits({ s.value("db/timeoutSec", 15).toInt(), s.value("db/maxRows", 500).toInt() });
    m_status = QStringLiteral("Aucun serveur connecté");
    connect(remote, &RemoteController::changed, this, &DbController::onRemoteChanged);
    connect(remote, &RemoteController::serversChanged, this, &DbController::onServersChanged);
    if (remote->tasks())
        connect(remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) { if (id == m_task && m_cancel) m_cancel->store(true); });
    onServersChanged();
}

DbController::~DbController() {
    if (m_cancel) m_cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

bool DbController::canEdit() const { return m_remote->connected() && !m_remote->activeServerId().isEmpty(); }
bool DbController::secretStoreAvailable() const { return SecretStore::available(); }

QList<DbProfile> DbController::allProfiles() const {
    QList<DbProfile> out;
    const QJsonArray arr = QJsonDocument::fromJson(QSettings{}.value("db/profiles").toByteArray()).array();
    for (const QJsonValue &v : arr) out << profileFromVariant(v.toObject().toVariantMap());
    return out;
}

void DbController::storeProfiles(const QList<DbProfile> &list) {
    QJsonArray arr;
    for (const DbProfile &p : list) arr << toJson(p);
    QSettings{}.setValue("db/profiles", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool DbController::findProfile(const QString &id, DbProfile *out) const {
    if (id.isEmpty() || !canEdit()) return false;
    for (const DbProfile &p : allProfiles())
        if (p.id == id && p.serverId == m_remote->activeServerId()) { if (out) *out = p; return true; }
    return false;
}

QVariantList DbController::profiles() const {
    QVariantList out;
    if (!canEdit()) return out;
    for (const DbProfile &p : allProfiles())
        if (p.serverId == m_remote->activeServerId()) out << profileToVariant(p);
    return out;
}

QVariantMap DbController::selected() const {
    DbProfile p;
    return findProfile(m_selectedId, &p) ? profileToVariant(p) : QVariantMap();
}

QVariantList DbController::numeric() const {
    QVariantList out;
    for (bool b : m_result.numeric) out << b;
    return out;
}

void DbController::onServersChanged() {
    // A removed server takes its database profiles (and their passwords) with it.
    QStringList ids;
    for (const QVariant &s : m_remote->servers()) ids << s.toMap().value("id").toString();
    QList<DbProfile> keep;
    bool pruned = false;
    for (const DbProfile &p : allProfiles()) {
        if (ids.contains(p.serverId)) { keep << p; continue; }
        pruned = true;
        if (p.hasPassword) SecretStore::remove(secretKey(p.id));
    }
    if (pruned) { storeProfiles(keep); emit profilesChanged(); }
}

void DbController::resetSession(bool clearSelection) {
    if (m_cancel) m_cancel->store(true);
    ++m_seq;
    m_busy = false;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_result = DbResult();
    m_rows.clear(); m_schema.clear();
    m_resultInfo.clear(); m_schemaStatus.clear(); m_error.clear();
    m_roleCheck.clear();
    if (clearSelection) m_selectedId.clear();
    emit roleChanged();
}

void DbController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) { emit profilesChanged(); return; }
    m_token = m_remote->connected() ? m_remote->connectionToken() : -1;
    resetSession(true);
    m_status = m_remote->connected()
        ? (m_remote->activeServerId().isEmpty() ? QStringLiteral("Enregistrez ce serveur pour créer des profils de bases de données.") : QStringLiteral("Choisissez ou créez un profil."))
        : QStringLiteral("Source déconnectée ou changée.");
    emit changed(); emit resultChanged(); emit schemaChanged(); emit profilesChanged();
}

void DbController::setWindowActive(bool active) { m_active = active; }

QString DbController::saveProfile(const QVariantMap &map, const QString &password, bool clearPassword) {
    if (!canEdit()) return QStringLiteral("Connectez un serveur enregistré d'abord.");
    DbProfile p = profileFromVariant(map);
    p.serverId = m_remote->activeServerId();
    QList<DbProfile> list = allProfiles();
    int index = -1;
    for (int i = 0; i < list.size(); ++i) if (list[i].id == p.id && list[i].serverId == p.serverId) index = i;
    const bool isNew = index < 0;
    if (isNew) p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (p.engine == DbEngine::Sqlite) { p.host.clear(); p.user.clear(); p.database.clear(); p.port = 5432; }
    else p.path.clear();
    const QString invalid = validateProfile(p);
    if (!invalid.isEmpty()) return invalid;

    const DbProfile old = isNew ? DbProfile() : list[index];
    const bool sameTarget = !isNew && old.engine == p.engine && old.host == p.host && old.port == p.port && old.user == p.user && old.database == p.database;
    p.hasPassword = !isNew && old.hasPassword && sameTarget && !clearPassword;   // never send an old password to a new target
    if (p.engine == DbEngine::Sqlite) { p.hasPassword = false; }
    if (!password.isEmpty() && p.engine == DbEngine::Postgres) {
        if (!SecretStore::available()) return SecretStore::unavailableReason();
        const SecretStore::Result r = SecretStore::save(secretKey(p.id), password);
        if (!r.ok) return r.error.isEmpty() ? QStringLiteral("Le mot de passe n'a pas pu être enregistré.") : r.error;
        p.hasPassword = true;
    } else if (!p.hasPassword && !isNew && old.hasPassword) {
        SecretStore::remove(secretKey(p.id));
    }
    if (isNew) list << p; else list[index] = p;
    storeProfiles(list);
    // Whatever was shown belonged to the previous profile (or to the previous settings of this one): nothing of it may stay under the new name.
    resetSession(false);
    m_selectedId = p.id;
    m_error.clear();
    m_status = QStringLiteral("Profil « %1 » enregistré.").arg(p.name);
    emit profilesChanged(); emit changed(); emit resultChanged(); emit schemaChanged();
    return {};
}

void DbController::removeProfile(const QString &id) {
    if (!canEdit()) return;
    QList<DbProfile> list = allProfiles();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id != id || list[i].serverId != m_remote->activeServerId()) continue;
        if (list[i].hasPassword) SecretStore::remove(secretKey(id));
        list.removeAt(i);
        break;
    }
    storeProfiles(list);
    if (m_selectedId == id) { resetSession(true); m_status = QStringLiteral("Profil supprimé."); emit changed(); emit resultChanged(); emit schemaChanged(); }
    emit profilesChanged();
}

void DbController::select(const QString &id) {
    if (m_busy || id == m_selectedId) return;
    DbProfile p;
    if (!findProfile(id, &p)) return;
    resetSession(false);
    m_selectedId = id;
    m_status = QStringLiteral("Profil « %1 » · lecture seule").arg(p.name);
    emit changed(); emit resultChanged(); emit schemaChanged();
    loadSchema();
}

void DbController::setLimits(int timeoutSec, int maxRows) {
    m_limits = clampLimits({ timeoutSec, maxRows });
    QSettings s;
    s.setValue("db/timeoutSec", m_limits.timeoutSec);
    s.setValue("db/maxRows", m_limits.maxRows);
    emit limitsChanged();
}

QString DbController::selectAllFor(const QVariantMap &o) const {
    const QString name = o.value("name").toString(), schema = o.value("schema").toString();
    const QString target = schema.isEmpty() ? quoteIdentifier(name) : quoteIdentifier(schema) + QLatin1Char('.') + quoteIdentifier(name);
    return QStringLiteral("SELECT * FROM ") + target + QStringLiteral(" LIMIT 100");
}

void DbController::loadSchema() {
    DbProfile p;
    if (m_busy || !findProfile(m_selectedId, &p)) return;
    QString password;
    if (p.hasPassword) {
        const SecretStore::Result r = SecretStore::load(secretKey(p.id));
        if (!r.ok) { m_schemaStatus = QStringLiteral("Mot de passe introuvable dans le stockage protégé : modifiez le profil."); emit schemaChanged(); return; }
        password = r.secret;
    }
    const QString marker = QStringLiteral("<<FQ-NULL-") + randomToken(8) + QStringLiteral(">>");
    const QByteArray script = buildSchemaScript(p, password, m_limits, marker, QStringLiteral("FQ") + randomToken(12));
    m_schemaStatus = QStringLiteral("Lecture du schéma…");
    emit schemaChanged();
    execute(script, 1, p, marker, 0);
}

void DbController::checkRole() {
    DbProfile p;
    if (m_busy || !findProfile(m_selectedId, &p)) return;
    QString password;
    if (p.hasPassword) {
        const SecretStore::Result r = SecretStore::load(secretKey(p.id));
        if (!r.ok) return;   // loadSchema already told the user
        password = r.secret;
    }
    const QString marker = QStringLiteral("<<FQ-NULL-") + randomToken(8) + QStringLiteral(">>");
    const QByteArray script = buildRoleCheckScript(p, password, m_limits, marker, QStringLiteral("FQ") + randomToken(12));
    execute(script, 2, p, marker, 0);
}

void DbController::runQuery(const QString &sql) {
    if (m_busy) return;
    DbProfile p;
    if (!findProfile(m_selectedId, &p)) { m_error = QStringLiteral("Choisissez un profil."); emit changed(); return; }
    const QString refusal = readOnlyRefusal(sql, p.engine);
    if (!refusal.isEmpty()) { m_error = refusal; emit changed(); return; }
    QString password;
    if (p.hasPassword) {
        const SecretStore::Result r = SecretStore::load(secretKey(p.id));
        if (!r.ok) { m_error = QStringLiteral("Mot de passe introuvable dans le stockage protégé : modifiez le profil."); emit changed(); return; }
        password = r.secret;
    }
    const QString marker = QStringLiteral("<<FQ-NULL-") + randomToken(8) + QStringLiteral(">>");
    QByteArray script;
    for (int attempt = 0; attempt < 3 && script.isEmpty(); ++attempt) {
        const QString delimiter = QStringLiteral("FQ") + randomToken(12);
        script = p.engine == DbEngine::Sqlite ? buildSqliteScript(p, sql, m_limits, delimiter) : buildPostgresScript(p, password, sql, m_limits, marker, delimiter);
    }
    if (script.isEmpty()) { m_error = QStringLiteral("La requête n'a pas pu être préparée."); emit changed(); return; }
    int taskId = 0;
    if (TaskManager *tasks = m_remote->tasks()) {
        TaskManager::Spec spec;
        spec.kind = QStringLiteral("dbQuery");
        spec.title = QStringLiteral("Requête · ") + p.name;   // the SQL text itself is never put in the task list
        spec.source = m_remote->serverLabel();
        spec.destination = p.name;
        spec.cancellable = true;
        spec.guards = false; spec.session = true;   // read-only query: leaving loses nothing and there is nothing to verify afterwards
        spec.generation = m_remote->connectionToken();
        taskId = m_task = tasks->create(spec);
        tasks->start(taskId);
    }
    m_error.clear();
    m_status = QStringLiteral("Exécution sur le serveur… (limite %1 s)").arg(m_limits.timeoutSec);
    execute(script, 0, p, marker, taskId);
}

void DbController::cancelQuery() {
    if (!m_busy) return;
    if (m_task != 0 && m_remote->tasks()) m_remote->tasks()->requestCancel(m_task);
    else if (m_cancel) m_cancel->store(true);
}

void DbController::execute(const QByteArray &script, int mode, const DbProfile &profile, const QString &marker, int taskId) {
    if (!m_remote->connected()) return;
    m_token = m_remote->connectionToken();
    const int token = m_token, seq = ++m_seq;
    const ConnectionRequest request = m_remote->connectionRequest();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    auto slot = m_session;
    const DbLimits limits = m_limits;
    const DbEngine engine = profile.engine;
    const DbProfile profileCopy = profile;
    const bool isSchema = mode == 1, isRole = mode == 2;
    m_busy = true;
    emit changed();

    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, seq, token, taskId, isSchema, isRole, engine, limits] {
        const Outcome r = watcher->result();
        watcher->deleteLater();
        TaskManager *tasks = m_remote->tasks();
        if (seq != m_seq || !m_remote->connected() || token != m_remote->connectionToken()) {
            if (tasks && taskId) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
            return;
        }
        m_busy = false;
        QString failure;
        if (r.cancelled || r.run.code == QLatin1String("cancelled")) {
            if (isRole) { emit changed(); return; }
            if (isSchema) m_schemaStatus = QStringLiteral("Lecture du schéma annulée.");
            else { m_status = QStringLiteral("Requête annulée."); m_error.clear(); }
            if (tasks && taskId) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
            emit changed(); emit schemaChanged();
            return;
        }
        if (!r.openError.isEmpty()) failure = r.openError;
        else if (r.run.status != SshResult::Status::Ok) failure = r.run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : r.run.message;
        else if (!r.run.truncated && r.run.code != QLatin1String("exit:0")) failure = describeDbFailure(engine, exitCodeOf(r.run.code), r.run.message, limits.timeoutSec);
        if (isRole) {
            // Never an error for the user: a check that could not run is said so and nothing else changes.
            if (!failure.isEmpty()) { RoleCheck unknown; unknown.level = QStringLiteral("unknown"); unknown.summary = QStringLiteral("Droits du rôle non vérifiés : %1").arg(failure.section(QLatin1Char('\n'), 0, 0).left(200)); m_roleCheck = roleCheckToVariant(unknown); }
            else m_roleCheck = roleCheckToVariant(r.role);
            emit changed(); emit roleChanged();
            return;
        }
        if (isSchema) {
            if (!failure.isEmpty()) { m_schema.clear(); m_schemaStatus = failure; }
            else { m_schema = schemaToVariant(r.objects); m_schemaStatus = m_schema.isEmpty() ? QStringLiteral("Aucune table.") : QString(); }
            emit changed(); emit schemaChanged();
            if (failure.isEmpty()) checkRole();   // what can this role do? asked once per opened profile
            return;
        }
        if (!failure.isEmpty()) {
            m_error = failure;
            m_status = QStringLiteral("Échec.");
            if (tasks && taskId) tasks->fail(taskId, failure.section(QLatin1Char('\n'), 0, 0).left(200));
            emit changed();
            return;
        }
        m_result = r.result;
        m_rows.clear();
        for (const QVariantList &row : r.result.rows) {
            QVariantList cells;
            for (const QVariant &c : row) cells << (c.isNull() ? QVariant() : QVariant(cellText(c, 2000)));
            m_rows << QVariant(cells);
        }
        m_resultInfo = r.result.columns.isEmpty() ? QStringLiteral("Aucun résultat tabulaire.")
                                                  : QStringLiteral("%1 ligne(s), %2 colonne(s)").arg(m_rows.size()).arg(r.result.columns.size());
        if (r.result.truncated) m_resultInfo += r.run.truncated ? QStringLiteral(" · tronqué (sortie > 4 Mio)") : QStringLiteral(" · tronqué (limite %1 lignes)").arg(limits.maxRows);
        m_status = QStringLiteral("Terminé · lecture seule");
        if (tasks && taskId) tasks->succeed(taskId, m_resultInfo);
        emit changed(); emit resultChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, request, script, cancel, isSchema, isRole, engine, limits, marker, profileCopy] {
        Outcome out;
        if (cancel->load()) { out.cancelled = true; return out; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { out.openError = opened.message; return out; }
        }
        out.run = session.runScript(script, kMaxDbOutput, [cancel] { return !cancel->load(); });
        if (out.run.status != SshResult::Status::Ok || out.run.code == QLatin1String("cancelled")) return out;
        if (!out.run.truncated && out.run.code != QLatin1String("exit:0")) return out;
        if (isRole) out.role = assessRoleCheck(profileCopy, out.run.data, marker);
        else if (isSchema) out.objects = parseSchema(engine, out.run.data, marker);
        else out.result = engine == DbEngine::Sqlite ? parseSqliteQuote(out.run.data, limits.maxRows, out.run.truncated)
                                                     : parsePostgresCsv(out.run.data, marker, limits.maxRows, out.run.truncated);
        return out;
    }));
}

}  // namespace fortiq
