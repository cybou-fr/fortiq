#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "license/LicenseController.h"

using namespace fortiq::license;

namespace {

constexpr qint64 kStore = 1234, kIntro = 111, kPro = 222;
const QString kKey = QStringLiteral("38b1460a-5104-4067-a91d-77b872934d51");

Config config() {
    Config c;
    c.storeId = kStore; c.introVariantId = kIntro; c.proVariantId = kPro;
    c.introCheckoutUrl = QStringLiteral("https://shop.lemonsqueezy.com/checkout/buy/intro");
    c.proCheckoutUrl = QStringLiteral("https://shop.lemonsqueezy.com/checkout/buy/pro");
    c.storeSlug = QStringLiteral("shop");
    return c;
}

// A License API answer as the provider documents it.
QByteArray answer(const QString &flag, bool ok, const QString &error, const QString &status, const QString &expires, const QString &instance,
                  qint64 store = kStore, qint64 variant = kIntro) {
    QJsonObject key{ { "id", 1 }, { "status", status }, { "key", kKey }, { "activation_limit", 1 }, { "activation_usage", 1 },
                     { "created_at", "2026-10-01T10:00:00.000000Z" }, { "expires_at", expires.isEmpty() ? QJsonValue() : QJsonValue(expires) } };
    QJsonObject o{ { flag, ok }, { "error", error.isEmpty() ? QJsonValue() : QJsonValue(error) }, { "license_key", key },
                   { "meta", QJsonObject{ { "store_id", store }, { "order_id", 2 }, { "product_id", 4 }, { "variant_id", variant },
                                          { "customer_name", "John Doe" }, { "customer_email", "john@example.org" } } } };
    if (!instance.isEmpty()) o.insert("instance", QJsonObject{ { "id", instance }, { "name", "FORTIQ" }, { "created_at", "2026-10-01T10:00:00.000000Z" } });
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// A License API of the test: a loopback HTTP server whose answers the test scripts.
class FakeApi : public QTcpServer {
public:
    struct Hit { QString path; QMap<QString, QString> form; QByteArray raw; };
    QList<Hit> hits;
    std::function<QPair<int, QByteArray>(const Hit &)> handler;
    bool dropConnections = false;             // accept and close without an answer
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    int count(const QString &path) const { int n = 0; for (const Hit &h : hits) n += h.path == path; return n; }

protected:
    void incomingConnection(qintptr handle) override {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int headEnd = buffer->indexOf("\r\n\r\n");
            if (headEnd < 0) return;
            const QByteArray head = buffer->left(headEnd);
            const int cl = QRegularExpression("Content-Length: *(\\d+)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(head)).captured(1).toInt();
            if (buffer->size() < headEnd + 4 + cl) return;
            Hit hit;
            hit.raw = *buffer;
            hit.path = QString::fromLatin1(head.split(' ').value(1));
            for (const QByteArray &pair : buffer->mid(headEnd + 4, cl).split('&')) {
                const int eq = pair.indexOf('=');
                if (eq > 0) hit.form.insert(QUrl::fromPercentEncoding(pair.left(eq)), QUrl::fromPercentEncoding(pair.mid(eq + 1)));
            }
            hits.append(hit);
            buffer->clear();
            if (dropConnections) { socket->disconnectFromHost(); return; }
            const auto [status, body] = handler ? handler(hit) : qMakePair(500, QByteArray());
            socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                          + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

QDateTime t(const char *iso) { return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate).toUTC(); }

}  // namespace

class LicenseTest : public QObject {
    Q_OBJECT
    FakeApi api;
    QDateTime clock = t("2026-10-24T12:00:00Z");
    MemoryLicenseStore *store = nullptr;

    std::unique_ptr<LicenseController> make(bool required = true) {
        auto owned = std::make_unique<MemoryLicenseStore>();
        if (store) { owned->key = store->key; owned->cache = store->cache; }
        store = owned.get();
        auto c = std::make_unique<LicenseController>(config(), Policy(), std::move(owned), required);
        c->setClock([this] { return clock; });
        c->setInstanceName("FORTIQ test");
        c->client()->setTimeoutMs(3000);
        return c;
    }
    // A second run of the product on the same device: same stored data, a new controller.
    std::unique_ptr<LicenseController> restart(LicenseController *old) {
        const QString key = store->key; const QByteArray cache = store->cache;
        (void)old;
        auto owned = std::make_unique<MemoryLicenseStore>();
        owned->key = key; owned->cache = cache;
        store = owned.get();
        auto c = std::make_unique<LicenseController>(config(), Policy(), std::move(owned), true);
        c->setClock([this] { return clock; });
        c->client()->setTimeoutMs(3000);
        c->client()->setBaseUrl(api.url());
        return c;
    }
    void serve(std::function<QPair<int, QByteArray>(const FakeApi::Hit &)> h) { api.handler = std::move(h); api.hits.clear(); api.dropConnections = false; }
    static QPair<int, QByteArray> ok(const FakeApi::Hit &h, const QString &expires = QString(), qint64 variant = kIntro) {
        if (h.path == "/v1/licenses/activate") return { 200, answer("activated", true, "", "active", expires, "inst-1", kStore, variant) };
        if (h.path == "/v1/licenses/validate") return { 200, answer("valid", true, "", "active", expires, "inst-1", kStore, variant) };
        return { 200, answer("deactivated", true, "", "inactive", "", "") };
    }

private slots:
    void initTestCase() {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QVERIFY(api.listen(QHostAddress::LocalHost));
    }
    void init() { store = nullptr; clock = t("2026-10-24T12:00:00Z"); api.hits.clear(); api.dropConnections = false; }

    // ---- the pure part -----------------------------------------------------------------------------------------------------------
    void parsesTheDocumentedAnswers() {
        const Reply a = parseReply(Kind::Activate, answer("activated", true, "", "active", "", "47596ad9-a811-4ebf-ac8a-03fc7b6d2a17"));
        QCOMPARE(a.outcome, Reply::Outcome::Ok);
        QCOMPARE(a.status, QString("active")); QCOMPARE(a.instanceId, QString("47596ad9-a811-4ebf-ac8a-03fc7b6d2a17"));
        QCOMPARE(a.storeId, kStore); QCOMPARE(a.variantId, kIntro); QVERIFY(!a.expiresAt.isValid());
        QCOMPARE(a.activationLimit, 1); QCOMPARE(a.activationUsage, 1);
        // six fractional digits and a Z, as the provider writes them
        const Reply v = parseReply(Kind::Validate, answer("valid", true, "", "active", "2027-01-24T14:15:07.000000Z", "i"));
        QCOMPARE(v.expiresAt, t("2027-01-24T14:15:07Z"));
        // a refusal is a definitive answer, with the provider's text
        const Reply limit = parseReply(Kind::Activate, answer("activated", false, "This license key has reached the activation limit.", "active", "", ""));
        QCOMPARE(limit.outcome, Reply::Outcome::Refused);
        QVERIFY(limit.error.contains("activation limit"));
        // validate without an instance: "instance": null
        const Reply noInstance = parseReply(Kind::Validate, "{\"valid\":true,\"error\":null,\"license_key\":{\"status\":\"active\",\"expires_at\":null},\"instance\":null,\"meta\":{\"store_id\":1234,\"variant_id\":111}}");
        QCOMPARE(noInstance.outcome, Reply::Outcome::Ok); QVERIFY(noInstance.instanceId.isEmpty());
        // numbers may arrive as strings
        QCOMPARE(parseReply(Kind::Validate, "{\"valid\":true,\"error\":null,\"license_key\":{\"status\":\"active\"},\"meta\":{\"store_id\":\"1234\",\"variant_id\":\"222\"}}").variantId, 222);
        // no customer data is kept: the reply type has no field for it
        QVERIFY(!QString::fromLatin1(typeid(Reply).name()).isEmpty());
    }
    void malformedAnswersAreNotAnswers() {
        for (const char *body : { "", "not json", "[]", "{}", "{\"valid\":\"yes\"}", "{\"activated\":true}" /* wrong flag for validate */, "{\"errors\":[{\"status\":\"401\"}]}" })
            QCOMPARE(parseReply(Kind::Validate, body).outcome, Reply::Outcome::Malformed);
        QCOMPARE(parseReply(Kind::Validate, "{\"valid\":true}").outcome, Reply::Outcome::Ok);    // the flag alone is enough to be an answer
        QCOMPARE(parseReply(Kind::Validate, "{\"valid\":false}").outcome, Reply::Outcome::Refused);
    }
    void evaluatesWhatAnAnswerEntitles() {
        const QDateTime now = clock;
        auto eval = [&](const QByteArray &body, Kind k = Kind::Validate) { return evaluate(parseReply(k, body), config(), now); };
        auto active = eval(answer("valid", true, "", "active", "", "i"));
        QVERIFY(active.active()); QCOMPARE(active.tier, Tier::Intro);
        QCOMPARE(eval(answer("valid", true, "", "active", "", "i", kStore, kPro)).tier, Tier::Pro);
        // another store, another product, or no configuration at all: never entitles, whatever the status says
        QCOMPARE(eval(answer("valid", true, "", "active", "", "i", 999)).state, State::WrongProduct);
        QCOMPARE(eval(answer("valid", true, "", "active", "", "i", kStore, 333)).state, State::WrongProduct);
        QCOMPARE(evaluate(parseReply(Kind::Validate, answer("valid", true, "", "active", "", "i")), Config(), now).state, State::WrongProduct);
        // statuses of the key
        QCOMPARE(eval(answer("valid", true, "", "expired", "", "i")).state, State::Expired);
        QCOMPARE(eval(answer("valid", true, "", "disabled", "", "i")).state, State::Disabled);
        QCOMPARE(eval(answer("valid", true, "", "inactive", "", "i")).state, State::NotActivated);
        // a paid term that has ended, and one that still runs (a cancelled subscription keeps working until its end)
        QCOMPARE(eval(answer("valid", true, "", "active", "2026-10-24T11:59:59Z", "i")).state, State::Expired);
        QVERIFY(eval(answer("valid", true, "", "active", "2026-11-24T12:00:00Z", "i")).active());
        // refusals: the activation limit is a refusal of this device, the licence itself is fine
        auto limit = eval(answer("activated", false, "This license key has reached the activation limit.", "active", "", ""), Kind::Activate);
        QCOMPARE(limit.state, State::Refused); QVERIFY(limit.message.contains("appareils"));
        QCOMPARE(eval(answer("valid", false, "license_key not found.", "", "", "", 0, 0)).state, State::Refused);
        QCOMPARE(eval("not json").state, State::Refused);
    }
    void decidesWhatACachedAnswerStillAllows() {
        Policy p;
        Cache c;
        c.entitlement.state = State::Active; c.entitlement.tier = Tier::Pro;
        c.validatedAt = clock; c.lastSeen = clock;
        QCOMPARE(decide(c, clock, p).access, Access::Granted);
        QCOMPARE(decide(c, clock.addSecs(p.revalidateSeconds), p).access, Access::Granted);
        const Decision grace = decide(c, clock.addSecs(p.revalidateSeconds + 60), p);
        QCOMPARE(grace.access, Access::Grace); QVERIFY(grace.allowed()); QVERIFY(grace.reason.contains("Hors ligne"));
        QCOMPARE(decide(c, clock.addSecs(p.graceSeconds), p).access, Access::Grace);
        const Decision stale = decide(c, clock.addSecs(p.graceSeconds + 1), p);
        QCOMPARE(stale.access, Access::NeedsRefresh); QVERIFY(!stale.allowed());
        // the paid term ends offline: no grace past the expiry
        c.entitlement.expiresAt = clock.addSecs(3600);
        QCOMPARE(decide(c, clock.addSecs(1800), p).access, Access::Granted);
        QCOMPARE(decide(c, clock.addSecs(3600), p).access, Access::Denied);
        c.entitlement.expiresAt = QDateTime();
        // the clock was moved back: not trusted until an online answer
        c.lastSeen = clock.addDays(2);
        QCOMPARE(decide(c, clock, p).access, Access::NeedsRefresh);
        c.validatedAt = clock.addDays(2).addSecs(-400);
        QCOMPARE(decide(c, clock.addDays(2).addSecs(-p.clockToleranceSeconds + 1), p).access, Access::Granted);        // inside the tolerance
        c.lastSeen = clock;
        c.validatedAt = clock.addDays(1);                       // "confirmed in the future": the same distrust
        QCOMPARE(decide(c, clock, p).access, Access::NeedsRefresh);
        // not entitled or never confirmed
        Cache none;
        QCOMPARE(decide(none, clock, p).access, Access::Denied);
        c.entitlement.state = State::Disabled; c.validatedAt = clock;
        QCOMPARE(decide(c, clock, p).access, Access::Denied);
    }
    void theCacheRoundTrips() {
        Cache c;
        c.instanceId = "inst-9"; c.entitlement.state = State::Active; c.entitlement.tier = Tier::Pro; c.entitlement.expiresAt = t("2027-01-01T00:00:00Z");
        c.validatedAt = clock; c.lastSeen = clock.addSecs(60);
        bool ok = false;
        const Cache back = Cache::fromJson(c.toJson(), &ok);
        QVERIFY(ok); QCOMPARE(back.instanceId, c.instanceId); QCOMPARE(back.entitlement.tier, Tier::Pro);
        QCOMPARE(back.entitlement.expiresAt, c.entitlement.expiresAt); QCOMPARE(back.validatedAt, c.validatedAt); QCOMPARE(back.lastSeen, c.lastSeen);
        QVERIFY(!c.toJson().contains(kKey.toUtf8()));               // the cache never holds the key
        for (const char *bad : { "", "x", "[]", "{}", "{\"state\":99,\"tier\":0}", "{\"state\":1,\"tier\":0}" /* no validated time */ }) {
            Cache::fromJson(bad, &ok); QVERIFY2(!ok, bad);
        }
    }
    void helpersForTheScreen() {
        QCOMPARE(maskKey(kKey), QString::fromUtf8("••••-4d51"));
        QVERIFY(!maskKey(kKey).contains("38b1"));
        QCOMPARE(maskKey("short"), QString("*****"));
        QVERIFY(plausibleKey(kKey)); QVERIFY(plausibleKey("  ABCD-1234-EFGH  "));
        for (const char *bad : { "", "abc", "has space inside-1234", "semi;colon-12345678", "-leading-dash-1234", "trailing-dash-1234-", "a/b/c/d/e/f/g/h" })
            QVERIFY2(!plausibleKey(bad), bad);
        QVERIFY(!plausibleKey(QString(101, 'a')));
        QVERIFY(describeRefusal("This license key has reached the activation limit.").contains("appareils"));
        QVERIFY(describeRefusal("license_key not found.").contains("inconnue"));
        QVERIFY(describeRefusal("").contains("refusé"));
        QVERIFY(describeRefusal(QString(500, 'x')).size() < 260);
        Config c = config();
        QCOMPARE(c.portalUrl(), QString("https://shop.lemonsqueezy.com/billing"));
        for (const char *bad : { "", "evil.com/x", "a b", "-x", "x.y", "https://x" }) { c.storeSlug = bad; QVERIFY2(c.portalUrl().isEmpty(), bad); }
        QVERIFY(!Config().configured());
        c = config(); c.proVariantId = c.introVariantId; QVERIFY(!c.configured());
    }
    void theApiAddressIsNotNegotiable() {
        LicenseClient client;
        QCOMPARE(client.baseUrl(), QUrl("https://api.lemonsqueezy.com"));
        for (const char *url : { "https://evil.example", "http://api.lemonsqueezy.com", "https://api.lemonsqueezy.com:8443", "https://api.lemonsqueezy.com.evil.example", "http://192.0.2.1", "ftp://127.0.0.1", "" })
            QVERIFY2(!client.setBaseUrl(QUrl(url)), url);
        QCOMPARE(client.baseUrl(), QUrl("https://api.lemonsqueezy.com"));
        QVERIFY(client.setBaseUrl(QUrl("https://api.lemonsqueezy.com")));
        QVERIFY(client.setBaseUrl(QUrl("http://127.0.0.1:8080")));      // only this testing build
    }

    // ---- the controller against a scripted License API -------------------------------------------------------------------------
    void activatesWithAValidKey() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h, "2026-11-07T12:00:00Z"); });
        c->start();
        QCOMPARE(c->state(), QString("none")); QVERIFY(!c->entitled());
        c->activate("  " + kKey + "  ");
        QCOMPARE(c->state(), QString("checking")); QVERIFY(c->busy());
        QTRY_VERIFY(!c->busy());
        QVERIFY2(c->entitled(), qPrintable(c->message()));
        QCOMPARE(c->state(), QString("active")); QCOMPARE(c->tier(), QString("intro"));
        QVERIFY(c->expiresText().contains("2026")); QVERIFY(c->canRemember());
        QCOMPARE(api.hits.size(), 1);
        const auto hit = api.hits.first();
        QCOMPARE(hit.path, QString("/v1/licenses/activate"));
        QCOMPARE(hit.form.value("license_key"), kKey); QCOMPARE(hit.form.value("instance_name"), QString("FORTIQ test"));
        QVERIFY(!hit.raw.left(hit.raw.indexOf("\r\n")).contains(kKey.toUtf8()));          // the key is in the body, not in the address
        QVERIFY(hit.raw.contains("application/x-www-form-urlencoded"));
        QCOMPARE(store->key, kKey);                                                       // kept in the protected store
        bool parsed = false; const Cache kept = Cache::fromJson(store->cache, &parsed);
        QVERIFY(parsed); QCOMPARE(kept.instanceId, QString("inst-1")); QVERIFY(!QString::fromUtf8(store->cache).contains(kKey));
        QVERIFY(!c->message().contains(kKey)); QVERIFY(!c->maskedKey().contains("38b1")); QCOMPARE(c->maskedKey(), QString::fromUtf8("••••-4d51"));
    }
    void refusesWithoutAskingWhenItCannotWork() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start();
        c->activate("");                                   QVERIFY(c->message().contains("forme"));
        c->activate("has space inside-1234");              QVERIFY(c->message().contains("forme"));
        QCOMPARE(api.hits.size(), 0);
        LicenseController unconfigured(Config(), Policy(), std::make_unique<MemoryLicenseStore>(), true);
        unconfigured.client()->setBaseUrl(api.url());
        unconfigured.activate(kKey);
        QVERIFY(unconfigured.message().contains("pas encore configurée")); QVERIFY(!unconfigured.busy());
        QCOMPARE(api.hits.size(), 0);
    }
    void anActivationLimitIsExplainedAndNothingIsKept() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &) { return qMakePair(200, answer("activated", false, "This license key has reached the activation limit.", "active", "", "")); });
        c->start(); c->activate(kKey);
        QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QCOMPARE(c->state(), QString("none"));
        QVERIFY2(c->message().contains("appareils"), qPrintable(c->message()));
        QVERIFY(store->key.isEmpty() && store->cache.isEmpty());
    }
    void anUnknownKeyUsesTheProvidersTextForTheDecision() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &) { return qMakePair(404, QByteArray("{\"activated\":false,\"error\":\"license_key not found.\",\"license_key\":null,\"instance\":null,\"meta\":null}")); });
        c->start(); c->activate(kKey);
        QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QVERIFY2(c->message().contains("inconnue"), qPrintable(c->message()));
        QVERIFY(store->key.isEmpty());
    }
    void aKeyOfAnotherProductNeverEntitlesAndItsActivationIsReleased() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) {
            if (h.path == "/v1/licenses/activate") return qMakePair(200, answer("activated", true, "", "active", "", "inst-foreign", 777, 888));
            return qMakePair(200, answer("deactivated", true, "", "inactive", "", ""));
        });
        c->start(); c->activate(kKey);
        QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QVERIFY(c->message().contains("pas une licence FORTIQ"));
        QTRY_COMPARE(api.count("/v1/licenses/deactivate"), 1);                              // the instance created by mistake is given back
        QCOMPARE(api.hits.last().form.value("instance_id"), QString("inst-foreign"));
        QVERIFY(store->key.isEmpty());
        QVERIFY(!c->busy());                                                                // and the release did not leave the controller waiting
    }
    void serverTroubleChangesNothingAboutTheLicence() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy()); QVERIFY(c->entitled());
        for (const int status : { 500, 503, 429, 408 }) {
            serve([status](const FakeApi::Hit &) { return qMakePair(status, QByteArray("<html>busy</html>")); });
            c->validateNow(); QTRY_VERIFY(!c->busy());
            QVERIFY2(c->entitled(), qPrintable(QString::number(status)));
            QVERIFY(!c->message().isEmpty());
        }
        serve([](const FakeApi::Hit &) { return qMakePair(200, QByteArray("this is not the license api")); });
        c->validateNow(); QTRY_VERIFY(!c->busy());
        QVERIFY(c->entitled()); QVERIFY2(c->message().contains("inattendue"), qPrintable(c->message()));
        serve([](const FakeApi::Hit &) { return qMakePair(302, QByteArray()); });
        c->validateNow(); QTRY_VERIFY(!c->busy());
        QVERIFY(c->entitled()); QVERIFY(c->message().contains("inattendue"));
        api.dropConnections = true;                                                         // connection closed without an answer
        c->validateNow(); QTRY_VERIFY(!c->busy());
        QVERIFY(c->entitled()); QVERIFY(c->message().contains("serveur de licences"));
    }
    void aFreshLicenseSurvivesARestartWithoutTheNetwork() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h, "2026-11-07T12:00:00Z"); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy());
        api.close();                                                                         // the provider cannot be reached at all
        clock = clock.addSecs(3600);
        auto again = restart(c.get());
        again->start();
        QVERIFY(again->entitled()); QCOMPARE(again->state(), QString("active")); QCOMPARE(again->tier(), QString("intro"));
        QVERIFY(!again->busy());                                                             // nothing was asked: the answer is fresh
        QVERIFY(api.listen(QHostAddress::LocalHost, 0)); again->client()->setBaseUrl(api.url());   // (the port changed: not needed any more)
    }
    void offlineGraceThenARefreshIsRequired() {
        api.close(); QVERIFY(api.listen(QHostAddress::LocalHost));
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h, "2027-01-01T00:00:00Z", kPro); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy());
        QCOMPARE(c->tier(), QString("pro"));
        api.dropConnections = true;
        clock = clock.addSecs(2 * 24 * 3600);                                                // two days without the provider
        auto grace = restart(c.get());
        grace->start();
        QTRY_VERIFY(!grace->busy());                                                         // it tried, failed, said so
        QVERIFY(grace->entitled()); QCOMPARE(grace->state(), QString("grace"));
        clock = clock.addSecs(2 * 24 * 3600);                                                // four days: past the grace period
        grace->tick(); QTRY_VERIFY(!grace->busy());
        QVERIFY(!grace->entitled()); QCOMPARE(grace->state(), QString("refresh"));
        QVERIFY2(grace->message().contains("Internet") || grace->message().contains("serveur"), qPrintable(grace->message()));
        QVERIFY(store->key == kKey);                                                         // nothing was forgotten, nothing deleted
        serve([](const FakeApi::Hit &h) { return ok(h, "2027-01-01T00:00:00Z", kPro); });     // the provider is back
        grace->validateNow(); QTRY_VERIFY(!grace->busy());
        QVERIFY(grace->entitled()); QCOMPARE(grace->state(), QString("active"));
    }
    void aRefusalAtRevalidationLocksTheProductButKeepsTheKey() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy()); QVERIFY(c->entitled());
        struct Case { const char *status; const char *state; const char *word; };
        for (const Case &k : { Case{ "disabled", "disabled", "désactivée" }, Case{ "expired", "expired", "expiré" } }) {
            serve([&k](const FakeApi::Hit &) { return qMakePair(200, answer("valid", true, "", k.status, "", "inst-1")); });
            c->validateNow(); QTRY_VERIFY(!c->busy());
            QVERIFY(!c->entitled()); QCOMPARE(c->state(), QString(k.state)); QVERIFY2(c->message().contains(k.word), qPrintable(c->message()));
            QCOMPARE(store->key, kKey);
            serve([](const FakeApi::Hit &h) { return ok(h); });                              // the licence is reinstated (renewal, refund undone)
            c->validateNow(); QTRY_VERIFY(!c->busy()); QVERIFY(c->entitled());
        }
        serve([](const FakeApi::Hit &) { return qMakePair(404, QByteArray("{\"valid\":false,\"error\":\"license_key not found.\",\"license_key\":null,\"meta\":null}")); });
        c->validateNow(); QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QCOMPARE(c->state(), QString("refused"));
    }
    void aPaidTermThatEndsOfflineEndsTheAccess() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h, "2026-10-25T12:00:00Z"); });        // a day of Intro left
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy());
        api.dropConnections = true;
        clock = t("2026-10-25T12:00:01Z");                                                   // the 14 days are over, the machine is offline
        auto later = restart(c.get()); later->start(); QTRY_VERIFY(!later->busy());
        QVERIFY(!later->entitled()); QCOMPARE(later->state(), QString("expired")); QVERIFY(!later->message().isEmpty());
    }
    void aClockTurnedBackIsNotTrustedUntilTheProviderConfirms() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h, "2027-01-01T00:00:00Z"); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy());
        clock = clock.addDays(2); c->tick(); QTRY_VERIFY(!c->busy());                        // used two days later, seen by the product
        api.dropConnections = true;
        clock = clock.addDays(-2);                                                           // the clock is set back to "extend" the grace
        auto cheat = restart(c.get()); cheat->start(); QTRY_VERIFY(!cheat->busy());
        QVERIFY(!cheat->entitled()); QCOMPARE(cheat->state(), QString("refresh")); QVERIFY(cheat->message().contains("horloge") || cheat->message().contains("serveur"));
        serve([](const FakeApi::Hit &h) { return ok(h, "2027-01-01T00:00:00Z"); });
        cheat->validateNow(); QTRY_VERIFY(!cheat->busy());
        QVERIFY(cheat->entitled());
    }
    void releasingThisDevice() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy()); QVERIFY(c->entitled());
        serve([](const FakeApi::Hit &) { return qMakePair(400, QByteArray("{\"deactivated\":false,\"error\":\"Something is wrong.\",\"license_key\":null,\"meta\":null}")); });
        c->deactivateThisDevice(); QTRY_VERIFY(!c->busy());
        QVERIFY(c->entitled()); QVERIFY(!store->key.isEmpty());                              // a refusal keeps everything
        serve([](const FakeApi::Hit &) { return qMakePair(404, QByteArray("{\"deactivated\":false,\"error\":\"license_key instance not found.\",\"license_key\":null,\"meta\":null}")); });
        c->deactivateThisDevice(); QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QVERIFY(store->key.isEmpty());                              // the provider no longer knows this device: forget it here too
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->activate(kKey); QTRY_VERIFY(!c->busy()); QVERIFY(c->entitled());
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->deactivateThisDevice(); QTRY_VERIFY(!c->busy());
        QVERIFY(!c->entitled()); QCOMPARE(c->state(), QString("none")); QVERIFY(store->key.isEmpty() && store->cache.isEmpty());
        QCOMPARE(api.hits.last().form.value("instance_id"), QString("inst-1"));
        QVERIFY(c->message().contains("libéré"));
    }
    void forgettingLocallyAsksNobody() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->activate(kKey); QTRY_VERIFY(!c->busy());
        const int before = api.hits.size();
        c->forgetLocally();
        QVERIFY(!c->entitled()); QVERIFY(store->key.isEmpty() && store->cache.isEmpty());
        QCOMPARE(api.hits.size(), before);
        QVERIFY(c->message().contains("serveurs") && c->message().contains("données"));      // the user's servers and data are said to be untouched
    }
    void purchaseAndPortalLinks() {
        auto c = make();
        QList<QUrl> opened;
        c->setOpener([&opened](const QUrl &u) { opened.append(u); return true; });
        QVERIFY(c->buy("intro")); QVERIFY(c->buy("pro")); QVERIFY(c->manage());
        QCOMPARE(opened.value(0), QUrl("https://shop.lemonsqueezy.com/checkout/buy/intro"));
        QCOMPARE(opened.value(1), QUrl("https://shop.lemonsqueezy.com/checkout/buy/pro"));
        QCOMPARE(opened.value(2), QUrl("https://shop.lemonsqueezy.com/billing"));
        Config bad = config(); bad.introCheckoutUrl = "http://shop.example/buy"; bad.proCheckoutUrl = ""; bad.storeSlug = "";
        LicenseController other(bad, Policy(), std::make_unique<MemoryLicenseStore>(), true);
        other.setOpener([&opened](const QUrl &u) { opened.append(u); return true; });
        QVERIFY(!other.buy("intro")); QVERIFY(!other.buy("pro")); QVERIFY(!other.manage());
        QCOMPARE(opened.size(), 3);                                                          // a plain-http or empty link is never opened
        QVERIFY(!other.message().isEmpty());
        QVERIFY(!c->entitled());                                                             // opening a checkout unlocks nothing
    }
    void aSecondRequestWhileBusyIsIgnored() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->activate(kKey); c->activate(kKey); c->validateNow(); c->deactivateThisDevice();
        QTRY_VERIFY(!c->busy());
        QCOMPARE(api.hits.size(), 1);
    }
    void startWithNothingAsksNothing() {
        auto c = make(); QVERIFY(c->client()->setBaseUrl(api.url()));
        serve([](const FakeApi::Hit &h) { return ok(h); });
        c->start(); c->tick();
        QCOMPARE(c->state(), QString("none")); QCOMPARE(api.hits.size(), 0);
        QVERIFY(!c->required() || c->required());
        QVERIFY(c->configured());
    }
};

QTEST_MAIN(LicenseTest)
#include "LicenseTest.moc"
