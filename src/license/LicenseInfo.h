#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace fortiq::license {

// Licence of the product itself (docs/LICENSING.md, LIC-02..LIC-04): Lemon Squeezy sells and issues the keys, the client activates
// and validates them over HTTPS (the one direct external connection of the client; everything else goes through SSH).
// This file is the pure part: parsing the License API answers, deciding what an answer entitles, deciding what a cached answer still
// entitles when the network is not there. No network, no storage, no GUI.

enum class Kind { Activate, Validate, Deactivate };
enum class Tier { Unknown, Intro, Pro };

// What identifies the store's products. Public numbers from the Lemon Squeezy dashboard (never an API key or a webhook secret).
struct Config {
    qint64 storeId = 0;
    qint64 introVariantId = 0;
    qint64 proVariantId = 0;
    QString introCheckoutUrl;      // hosted checkout, https only
    QString proCheckoutUrl;
    QString storeSlug;             // sub-domain of <slug>.lemonsqueezy.com for the customer portal

    bool configured() const { return storeId > 0 && introVariantId > 0 && proVariantId > 0 && introVariantId != proVariantId; }
    Tier tierOf(qint64 variantId) const;
    QString portalUrl() const;     // https://<slug>.lemonsqueezy.com/billing, empty when the slug is not a plain sub-domain label
};

// Release policy. The values are PLACEHOLDERS until the owner decides (docs/LICENSING.md section 5): how long a licence that could not
// be re-checked keeps working must be decided and tested, not silently assumed.
struct Policy {
    qint64 revalidateSeconds = 24 * 3600;     // a fresh answer is this old at most while online
    qint64 graceSeconds = 3 * 24 * 3600;      // offline: the last good answer still counts this long
    qint64 clockToleranceSeconds = 5 * 60;    // a clock earlier than the last time the product ran by more than this is not trusted
};

// One answer of the License API, reduced to what the decision needs (no customer name or e-mail is kept).
struct Reply {
    enum class Outcome { Ok, Refused, Malformed };
    Outcome outcome = Outcome::Malformed;
    Kind kind = Kind::Validate;
    QString error;                 // the API's own text when it refused
    QString status;                // license_key.status: inactive | active | expired | disabled
    QString instanceId;
    qint64 activationLimit = -1;
    qint64 activationUsage = -1;
    QDateTime expiresAt;           // invalid = no expiry (a subscription key is renewed by the provider)
    qint64 storeId = 0;
    qint64 productId = 0;
    qint64 variantId = 0;
};
// `body` is the JSON the API returned (any HTTP status): "activated" / "valid" / "deactivated" true and no error = Ok;
// the flag false or an error text = Refused (a definitive answer); anything that is not that shape = Malformed.
Reply parseReply(Kind kind, const QByteArray &body);

enum class State { NotActivated, Active, Expired, Disabled, WrongProduct, Refused };
struct Entitlement {
    State state = State::NotActivated;
    Tier tier = Tier::Unknown;
    QDateTime expiresAt;
    QString message;               // French, for the user
    bool active() const { return state == State::Active; }
};
// What one reply entitles, at `now`. Only an Ok reply about OUR store and one of OUR two variants, with status "active" and an expiry
// in the future (or none), entitles. A cancelled subscription is still "active" until its paid term ends: only the key's own
// status and expiry count, never the subscription status.
Entitlement evaluate(const Reply &reply, const Config &config, const QDateTime &now);

// The last good answer, kept between runs (inside the protected store, because it is what grants offline access).
struct Cache {
    QString instanceId;
    Entitlement entitlement;
    QDateTime validatedAt;         // when the provider last confirmed it
    QDateTime lastSeen;            // the latest clock reading the product has seen (moving the clock back does not extend anything)
    QByteArray toJson() const;
    static Cache fromJson(const QByteArray &json, bool *ok = nullptr);
};

enum class Access { Granted, Grace, NeedsRefresh, Denied };
struct Decision {
    Access access = Access::Denied;
    QString reason;                // French; for Granted empty
    bool allowed() const { return access == Access::Granted || access == Access::Grace; }
};
// May the product run now on this cached answer? Granted: confirmed recently. Grace: not confirmed for a while (offline) but inside the
// grace period. NeedsRefresh: allowed only after an online confirmation (too old, or the clock went back). Denied: not entitled.
Decision decide(const Cache &cache, const QDateTime &now, const Policy &policy);

// "XXXX-…-1234": the key is a secret; screens and logs show this.
QString maskKey(const QString &key);
// Cheap shape check before anything is sent (letters, digits and dashes, 8..100 characters); the provider decides whether it is real.
bool plausibleKey(const QString &key);
// French text for a refusal of the provider (known English messages are translated; unknown ones are shown as received, shortened).
QString describeRefusal(const QString &apiError);

}  // namespace fortiq::license
