#include "LicenseController.h"

#include "../core/SecretStore.h"

#include <QDesktopServices>
#include <QSysInfo>
#include <QUrl>

namespace fortiq::license {

// ---- stores ---------------------------------------------------------------------------------------------------------------------
bool SecretLicenseStore::available() const { return SecretStore::available(); }
QString SecretLicenseStore::loadKey() { const auto r = SecretStore::load(QStringLiteral("license-key")); return r.ok ? r.secret : QString(); }
QByteArray SecretLicenseStore::loadCache() { const auto r = SecretStore::load(QStringLiteral("license-cache")); return r.ok ? r.secret.toUtf8() : QByteArray(); }
bool SecretLicenseStore::save(const QString &key, const QByteArray &cache) {
    return SecretStore::save(QStringLiteral("license-key"), key).ok && SecretStore::save(QStringLiteral("license-cache"), QString::fromUtf8(cache)).ok;
}
void SecretLicenseStore::clear() { SecretStore::remove(QStringLiteral("license-key")); SecretStore::remove(QStringLiteral("license-cache")); }

// ---- build configuration ---------------------------------------------------------------------------------------------------------
Config LicenseController::configFromBuild() {
    Config c;
#ifdef FORTIQ_LS_STORE_ID
    c.storeId = FORTIQ_LS_STORE_ID;
#endif
#ifdef FORTIQ_LS_INTRO_VARIANT
    c.introVariantId = FORTIQ_LS_INTRO_VARIANT;
#endif
#ifdef FORTIQ_LS_PRO_VARIANT
    c.proVariantId = FORTIQ_LS_PRO_VARIANT;
#endif
#ifdef FORTIQ_LS_INTRO_URL
    c.introCheckoutUrl = QStringLiteral(FORTIQ_LS_INTRO_URL);
#endif
#ifdef FORTIQ_LS_PRO_URL
    c.proCheckoutUrl = QStringLiteral(FORTIQ_LS_PRO_URL);
#endif
#ifdef FORTIQ_LS_STORE_SLUG
    c.storeSlug = QStringLiteral(FORTIQ_LS_STORE_SLUG);
#endif
    return c;
}

bool LicenseController::requiredByBuild() {
#ifdef FORTIQ_REQUIRE_LICENSE
    return true;
#else
    return false;
#endif
}

// ---- controller ------------------------------------------------------------------------------------------------------------------
LicenseController::LicenseController(const Config &config, const Policy &policy, std::unique_ptr<LicenseStore> store, bool required, QObject *parent)
    : QObject(parent), m_config(config), m_policy(policy), m_store(std::move(store)), m_client(this), m_required(required),
      m_instanceName(QStringLiteral("FORTIQ (%1)").arg(QSysInfo::productType())) {
    m_now = [] { return QDateTime::currentDateTimeUtc(); };
    m_open = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    m_timer.setInterval(60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &LicenseController::onTick);
    connect(&m_client, &LicenseClient::finished, this, &LicenseController::onFinished);
}

Decision LicenseController::decision() const { return decide(m_cache, m_now(), m_policy); }

bool LicenseController::entitled() const { return decision().allowed() && !m_key.isEmpty(); }       // a re-check in progress does not lock anything

QString LicenseController::state() const {
    if (m_busy && !decision().allowed()) return QStringLiteral("checking");
    if (m_key.isEmpty() && !m_cache.entitlement.active()) return QStringLiteral("none");
    switch (decision().access) {
    case Access::Granted: return QStringLiteral("active");
    case Access::Grace: return QStringLiteral("grace");
    case Access::NeedsRefresh: return QStringLiteral("refresh");
    case Access::Denied: break;
    }
    switch (m_cache.entitlement.state) {
    case State::Expired: return QStringLiteral("expired");
    case State::Disabled: return QStringLiteral("disabled");
    case State::WrongProduct: return QStringLiteral("wrongproduct");
    case State::Refused: return QStringLiteral("refused");
    case State::NotActivated: return QStringLiteral("notactivated");
    case State::Active: return QStringLiteral("expired");       // an answer that was valid but whose term has ended meanwhile
    }
    return QStringLiteral("none");
}

QString LicenseController::tier() const {
    return m_cache.entitlement.tier == Tier::Intro ? QStringLiteral("intro") : m_cache.entitlement.tier == Tier::Pro ? QStringLiteral("pro") : QString();
}
QString LicenseController::tierLabel() const {
    return m_cache.entitlement.tier == Tier::Intro ? QStringLiteral("Intro (14 jours)") : m_cache.entitlement.tier == Tier::Pro ? QStringLiteral("Pro (abonnement annuel)") : QString();
}
QString LicenseController::expiresText() const {
    if (!m_cache.entitlement.active()) return {};
    return m_cache.entitlement.expiresAt.isValid()
        ? QStringLiteral("Valable jusqu'au %1").arg(m_cache.entitlement.expiresAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")))
        : QStringLiteral("Sans date d'expiration (abonnement renouvelé par le vendeur)");
}

void LicenseController::setMessage(const QString &text) { m_message = text; }

void LicenseController::persist() {
    if (m_key.isEmpty()) return;
    m_store->save(m_key, m_cache.toJson());
    m_lastSaved = m_now();
}

void LicenseController::start() {
    m_key = m_store->loadKey().trimmed();
    bool ok = false;
    m_cache = m_key.isEmpty() ? Cache() : Cache::fromJson(m_store->loadCache(), &ok);
    if (!ok) m_cache = Cache();
    setMessage(m_store->available() ? QString() : QStringLiteral("Ce système n'a pas de coffre protégé : la licence ne sera pas conservée après la fermeture."));
    m_timer.start();
    emit changed();
    onTick();
}

void LicenseController::onTick() {
    const QDateTime now = m_now().toUTC();
    if (!m_key.isEmpty() && m_cache.validatedAt.isValid() && (!m_cache.lastSeen.isValid() || now > m_cache.lastSeen)) {
        m_cache.lastSeen = now;
        if (!m_lastSaved.isValid() || m_lastSaved.secsTo(now) > 600) persist();
    }
    if (!m_key.isEmpty() && !m_busy && (decision().access != Access::Granted || !m_cache.validatedAt.isValid()))
        validateNow();
    emit changed();
}

void LicenseController::validateNow() {
    if (m_busy) return;
    if (m_key.isEmpty()) { setMessage(QStringLiteral("Aucune clé de licence à vérifier.")); emit changed(); return; }
    m_busy = true;
    m_pending = Kind::Validate;
    setMessage(QString());
    emit changed();
    m_client.validate(m_key, m_cache.instanceId);
}

void LicenseController::activate(const QString &keyIn) {
    if (m_busy) return;
    const QString key = keyIn.trimmed();
    if (!m_config.configured()) { setMessage(QStringLiteral("La boutique FORTIQ n'est pas encore configurée dans cette version : l'activation est impossible.")); emit changed(); return; }
    if (!plausibleKey(key)) { setMessage(QStringLiteral("Cette clé n'a pas la forme d'une clé de licence (lettres, chiffres et tirets).")); emit changed(); return; }
    m_busy = true;
    m_pending = Kind::Activate;
    m_pendingKey = key;
    setMessage(QString());
    emit changed();
    m_client.activate(key, m_instanceName);
}

void LicenseController::deactivateThisDevice() {
    if (m_busy) return;
    if (m_key.isEmpty() || m_cache.instanceId.isEmpty()) { forgetLocally(); return; }
    m_busy = true;
    m_pending = Kind::Deactivate;
    setMessage(QString());
    emit changed();
    m_client.deactivate(m_key, m_cache.instanceId);
}

void LicenseController::forgetLocally() {
    m_store->clear();
    m_key.clear();
    m_cache = Cache();
    m_pendingKey.clear();
    setMessage(QStringLiteral("La licence a été retirée de cet appareil. Vos serveurs et vos données ne sont pas touchés."));
    emit changed();
}

bool LicenseController::buy(const QString &tierName) {
    const QString url = tierName == QLatin1String("pro") ? m_config.proCheckoutUrl : m_config.introCheckoutUrl;
    const QUrl target(url);
    if (!target.isValid() || target.scheme() != QLatin1String("https") || target.host().isEmpty()) {
        setMessage(QStringLiteral("Le lien d'achat n'est pas configuré dans cette version."));
        emit changed();
        return false;
    }
    return m_open(target);
}

bool LicenseController::manage() {
    const QString url = m_config.portalUrl();
    if (url.isEmpty()) { setMessage(QStringLiteral("Le portail client n'est pas configuré dans cette version.")); emit changed(); return false; }
    return m_open(QUrl(url));
}

void LicenseController::onFinished(const LicenseClient::Result &result) {
    if (m_cleanup > 0 && result.kind == Kind::Deactivate && m_pending != Kind::Deactivate) { --m_cleanup; return; }   // a foreign key activated by mistake, released
    applyAnswer(result);
}

void LicenseController::applyAnswer(const LicenseClient::Result &result) {
    m_busy = false;
    const QDateTime now = m_now().toUTC();
    const Reply &r = result.reply;
    // No usable answer: nothing is learned about the licence. What was granted stays as the policy allows; the user is told why.
    if (result.transient || r.outcome == Reply::Outcome::Malformed) {
        setMessage(result.transport.isEmpty() ? QStringLiteral("Réponse inattendue du serveur de licences.") : result.transport);
        emit changed();
        return;
    }
    const Entitlement ent = evaluate(r, m_config, now);
    switch (result.kind) {
    case Kind::Activate:
        if (r.outcome == Reply::Outcome::Ok && ent.active()) {
            m_key = m_pendingKey;
            m_cache = Cache{ r.instanceId, ent, now, now };
            persist();
            setMessage(QStringLiteral("Licence activée."));
        } else {
            if (r.outcome == Reply::Outcome::Ok && !r.instanceId.isEmpty()) {          // an instance of someone else's product was created: release it
                ++m_cleanup;
                m_client.deactivate(m_pendingKey, r.instanceId);
            }
            setMessage(ent.message);
        }
        m_pendingKey.clear();
        break;
    case Kind::Validate:
        m_cache.entitlement = ent;
        m_cache.validatedAt = now;
        m_cache.lastSeen = now;
        if (!r.instanceId.isEmpty()) m_cache.instanceId = r.instanceId;
        persist();
        setMessage(ent.active() ? QString() : ent.message);
        break;
    case Kind::Deactivate:
        if (r.outcome == Reply::Outcome::Ok || r.error.contains(QLatin1String("instance"), Qt::CaseInsensitive)) {
            forgetLocally();
            setMessage(QStringLiteral("Cet appareil a été libéré : la clé peut être activée ailleurs."));
        } else {
            setMessage(describeRefusal(r.error));
        }
        break;
    }
    emit changed();
}

}  // namespace fortiq::license
