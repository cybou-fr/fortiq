#include "LicenseInfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace fortiq::license {

Tier Config::tierOf(qint64 variantId) const {
    if (variantId <= 0) return Tier::Unknown;
    if (variantId == introVariantId) return Tier::Intro;
    if (variantId == proVariantId) return Tier::Pro;
    return Tier::Unknown;
}

QString Config::portalUrl() const {
    static const QRegularExpression label(QStringLiteral("^[a-z0-9]([a-z0-9-]{0,60}[a-z0-9])?$"));
    return label.match(storeSlug).hasMatch() ? QStringLiteral("https://%1.lemonsqueezy.com/billing").arg(storeSlug) : QString();
}

namespace {
QDateTime parseDate(const QJsonValue &v) {
    if (!v.isString()) return {};
    QDateTime d = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    if (!d.isValid()) d = QDateTime::fromString(v.toString(), Qt::ISODate);
    return d.isValid() ? d.toUTC() : QDateTime();
}
qint64 number(const QJsonValue &v) { return v.isDouble() ? static_cast<qint64>(v.toDouble()) : (v.isString() ? v.toString().toLongLong() : 0); }
}  // namespace

Reply parseReply(Kind kind, const QByteArray &body) {
    Reply r;
    r.kind = kind;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return r;
    const QJsonObject o = doc.object();
    const QString flagName = kind == Kind::Activate ? QStringLiteral("activated") : kind == Kind::Validate ? QStringLiteral("valid") : QStringLiteral("deactivated");
    if (!o.value(flagName).isBool()) return r;                       // not an answer of the License API
    const QJsonValue error = o.value(QStringLiteral("error"));
    r.error = error.isString() ? error.toString().trimmed() : QString();
    const QJsonObject key = o.value(QStringLiteral("license_key")).toObject();
    r.status = key.value(QStringLiteral("status")).toString().toLower();
    r.activationLimit = key.contains(QStringLiteral("activation_limit")) && key.value(QStringLiteral("activation_limit")).isDouble() ? number(key.value(QStringLiteral("activation_limit"))) : -1;
    r.activationUsage = key.value(QStringLiteral("activation_usage")).isDouble() ? number(key.value(QStringLiteral("activation_usage"))) : -1;
    r.expiresAt = parseDate(key.value(QStringLiteral("expires_at")));
    const QJsonValue instance = o.value(QStringLiteral("instance"));
    if (instance.isObject()) r.instanceId = instance.toObject().value(QStringLiteral("id")).toString();
    const QJsonObject meta = o.value(QStringLiteral("meta")).toObject();
    r.storeId = number(meta.value(QStringLiteral("store_id")));
    r.productId = number(meta.value(QStringLiteral("product_id")));
    r.variantId = number(meta.value(QStringLiteral("variant_id")));
    r.outcome = o.value(flagName).toBool() && r.error.isEmpty() ? Reply::Outcome::Ok : Reply::Outcome::Refused;
    return r;
}

QString describeRefusal(const QString &apiError) {
    const QString e = apiError.trimmed();
    const QString low = e.toLower();
    if (low.isEmpty()) return QStringLiteral("Le serveur de licences a refusé cette demande.");
    if (low.contains(QStringLiteral("activation limit"))) return QStringLiteral("Cette clé a atteint son nombre maximal d'appareils : désactivez-la sur un autre appareil, puis réessayez.");
    if (low.contains(QStringLiteral("not found")) || low.contains(QStringLiteral("invalid"))) return QStringLiteral("Clé de licence inconnue : vérifiez-la (reçue par e-mail après l'achat).");
    if (low.contains(QStringLiteral("expired"))) return QStringLiteral("Cette licence a expiré.");
    if (low.contains(QStringLiteral("disabled")) || low.contains(QStringLiteral("revoked"))) return QStringLiteral("Cette licence a été désactivée par le vendeur.");
    if (low.contains(QStringLiteral("instance"))) return QStringLiteral("Cet appareil n'est plus enregistré pour cette licence : activez-la de nouveau.");
    return QStringLiteral("Refus du serveur de licences : ") + e.left(160);
}

Entitlement evaluate(const Reply &r, const Config &config, const QDateTime &now) {
    Entitlement e;
    e.expiresAt = r.expiresAt;
    if (r.kind == Kind::Deactivate) {
        e.state = State::NotActivated;
        e.message = QStringLiteral("Cet appareil n'utilise plus la licence.");
        return e;
    }
    if (r.outcome == Reply::Outcome::Malformed) {
        e.state = State::Refused;
        e.message = QStringLiteral("Réponse inattendue du serveur de licences.");
        return e;
    }
    // Whatever the status says, a key of another store or of a product that is not FORTIQ never entitles.
    const bool ours = config.configured() && r.storeId == config.storeId && config.tierOf(r.variantId) != Tier::Unknown;
    if (!ours && (r.outcome == Reply::Outcome::Ok || r.storeId != 0)) {
        e.state = State::WrongProduct;
        e.message = QStringLiteral("Cette clé n'est pas une licence FORTIQ.");
        return e;
    }
    e.tier = config.tierOf(r.variantId);
    if (r.outcome == Reply::Outcome::Refused) {
        if (r.status == QLatin1String("expired")) { e.state = State::Expired; e.message = describeRefusal(QStringLiteral("expired")); }
        else if (r.status == QLatin1String("disabled")) { e.state = State::Disabled; e.message = describeRefusal(QStringLiteral("disabled")); }
        else { e.state = State::Refused; e.message = describeRefusal(r.error); }
        return e;
    }
    if (r.status == QLatin1String("disabled")) { e.state = State::Disabled; e.message = describeRefusal(QStringLiteral("disabled")); return e; }
    if (r.status == QLatin1String("expired") || (r.expiresAt.isValid() && r.expiresAt <= now.toUTC())) {
        e.state = State::Expired;
        e.message = r.expiresAt.isValid() ? QStringLiteral("Cette licence a expiré le %1.").arg(r.expiresAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")))
                                          : describeRefusal(QStringLiteral("expired"));
        return e;
    }
    if (r.status == QLatin1String("active")) {
        e.state = State::Active;
        return e;
    }
    e.state = State::NotActivated;       // "inactive": a real key that no device has activated yet
    e.message = QStringLiteral("Cette clé n'est pas encore activée sur cet appareil.");
    return e;
}

QByteArray Cache::toJson() const {
    QJsonObject o;
    o.insert(QStringLiteral("instance"), instanceId);
    o.insert(QStringLiteral("state"), static_cast<int>(entitlement.state));
    o.insert(QStringLiteral("tier"), static_cast<int>(entitlement.tier));
    o.insert(QStringLiteral("expires"), entitlement.expiresAt.isValid() ? entitlement.expiresAt.toUTC().toString(Qt::ISODate) : QString());
    o.insert(QStringLiteral("validated"), validatedAt.isValid() ? validatedAt.toUTC().toString(Qt::ISODate) : QString());
    o.insert(QStringLiteral("seen"), lastSeen.isValid() ? lastSeen.toUTC().toString(Qt::ISODate) : QString());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

Cache Cache::fromJson(const QByteArray &json, bool *ok) {
    Cache c;
    if (ok) *ok = false;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return c;
    const QJsonObject o = doc.object();
    const int state = o.value(QStringLiteral("state")).toInt(-1), tier = o.value(QStringLiteral("tier")).toInt(-1);
    if (state < 0 || state > static_cast<int>(State::Refused) || tier < 0 || tier > static_cast<int>(Tier::Pro)) return c;
    c.instanceId = o.value(QStringLiteral("instance")).toString();
    c.entitlement.state = static_cast<State>(state);
    c.entitlement.tier = static_cast<Tier>(tier);
    c.entitlement.expiresAt = parseDate(o.value(QStringLiteral("expires")));
    c.validatedAt = parseDate(o.value(QStringLiteral("validated")));
    c.lastSeen = parseDate(o.value(QStringLiteral("seen")));
    if (ok) *ok = c.validatedAt.isValid();
    return c;
}

Decision decide(const Cache &c, const QDateTime &nowIn, const Policy &p) {
    Decision d;
    const QDateTime now = nowIn.toUTC();
    if (!c.entitlement.active()) {
        d.access = Access::Denied;
        d.reason = c.entitlement.message.isEmpty() ? QStringLiteral("Aucune licence active sur cet appareil.") : c.entitlement.message;
        return d;
    }
    if (c.entitlement.expiresAt.isValid() && now >= c.entitlement.expiresAt) {
        d.access = Access::Denied;
        d.reason = QStringLiteral("Cette licence a expiré le %1.").arg(c.entitlement.expiresAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")));
        return d;
    }
    if (!c.validatedAt.isValid() || (c.lastSeen.isValid() && now.addSecs(p.clockToleranceSeconds) < c.lastSeen) || now.addSecs(p.clockToleranceSeconds) < c.validatedAt) {
        d.access = Access::NeedsRefresh;
        d.reason = QStringLiteral("L'horloge de l'ordinateur est en retard sur la dernière utilisation : une vérification en ligne est nécessaire.");
        return d;
    }
    const qint64 age = c.validatedAt.secsTo(now);
    if (age <= p.revalidateSeconds) { d.access = Access::Granted; return d; }
    if (age <= p.graceSeconds) {
        d.access = Access::Grace;
        d.reason = QStringLiteral("Hors ligne : licence confirmée il y a %1 h ; une vérification en ligne est nécessaire avant %2.")
                       .arg(age / 3600).arg(c.validatedAt.addSecs(p.graceSeconds).toLocalTime().toString(QStringLiteral("dd/MM HH:mm")));
        return d;
    }
    d.access = Access::NeedsRefresh;
    d.reason = QStringLiteral("La licence n'a pas pu être vérifiée depuis plus de %1 h : connectez-vous à Internet.").arg(p.graceSeconds / 3600);
    return d;
}

QString maskKey(const QString &key) {
    const QString k = key.trimmed();
    if (k.size() <= 8) return QString(k.size(), QLatin1Char('*'));
    return QStringLiteral("••••-") + k.right(4);
}

bool plausibleKey(const QString &key) {
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{6,98}[A-Za-z0-9]$"));
    return shape.match(key.trimmed()).hasMatch();
}

}  // namespace fortiq::license
