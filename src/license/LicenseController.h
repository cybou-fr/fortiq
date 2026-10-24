#pragma once

#include "LicenseClient.h"
#include "LicenseInfo.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace fortiq::license {

// Where the key and the last good answer are kept between runs. The real one is the protected store of the platform (Credential
// Manager, Android Keystore); nothing is ever written to QSettings or a plain file.
class LicenseStore {
public:
    virtual ~LicenseStore() = default;
    virtual bool available() const = 0;
    virtual QString loadKey() = 0;
    virtual QByteArray loadCache() = 0;
    virtual bool save(const QString &key, const QByteArray &cache) = 0;
    virtual void clear() = 0;
};
class SecretLicenseStore : public LicenseStore {
public:
    bool available() const override;
    QString loadKey() override;
    QByteArray loadCache() override;
    bool save(const QString &key, const QByteArray &cache) override;
    void clear() override;
};
class MemoryLicenseStore : public LicenseStore {     // tests, and platforms without a protected store (the licence then lasts for the run)
public:
    bool available() const override { return true; }
    QString loadKey() override { return key; }
    QByteArray loadCache() override { return cache; }
    bool save(const QString &k, const QByteArray &c) override { key = k; cache = c; ++saves; return true; }
    void clear() override { key.clear(); cache.clear(); }
    QString key;
    QByteArray cache;
    int saves = 0;
};

// Purchase, activation, restore, status and release of this device for the product licence (LIC-02..LIC-04, LIC-06). It decides only
// whether the desktop may be used; it never deletes or locks the SSH servers, the stored secrets or any data of the user (LICENSING.md,
// "Data safety"). With `required` false (development) the product works without a licence and the screen is only informative.
class LicenseController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool required READ required CONSTANT)
    Q_PROPERTY(bool entitled READ entitled NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)         // none | checking | active | grace | refresh | notactivated | expired | disabled | wrongproduct | refused
    Q_PROPERTY(QString tier READ tier NOTIFY changed)           // intro | pro | ""
    Q_PROPERTY(QString tierLabel READ tierLabel NOTIFY changed)
    Q_PROPERTY(QString expiresText READ expiresText NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY changed)
    Q_PROPERTY(QString maskedKey READ maskedKey NOTIFY changed)
    Q_PROPERTY(bool canRemember READ canRemember CONSTANT)      // a protected store exists: the licence survives a restart

public:
    LicenseController(const Config &config, const Policy &policy, std::unique_ptr<LicenseStore> store, bool required, QObject *parent = nullptr);
    static Config configFromBuild();
    static bool requiredByBuild();

    bool configured() const { return m_config.configured(); }
    bool required() const { return m_required; }
    bool entitled() const;
    QString state() const;
    QString tier() const;
    QString tierLabel() const;
    QString expiresText() const;
    QString message() const { return m_message; }
    bool busy() const { return m_busy; }
    bool hasKey() const { return !m_key.isEmpty(); }
    QString maskedKey() const { return maskKey(m_key); }
    bool canRemember() const { return m_store->available(); }

    // Reads what was kept, decides, and confirms online when the answer is old.
    Q_INVOKABLE void start();
    Q_INVOKABLE void activate(const QString &key);
    Q_INVOKABLE void validateNow();
    Q_INVOKABLE void deactivateThisDevice();     // frees one activation (device migration); the key stays valid elsewhere
    Q_INVOKABLE void forgetLocally();            // drops the key and the cache of this device without asking the provider
    Q_INVOKABLE bool buy(const QString &tier);   // opens the hosted checkout ("intro" | "pro"); false when it is not configured
    Q_INVOKABLE bool manage();                   // opens the customer portal

    // Seams for tests.
    LicenseClient *client() { return &m_client; }
    void setClock(std::function<QDateTime()> now) { m_now = std::move(now); }
    void setOpener(std::function<bool(const QUrl &)> opener) { m_open = std::move(opener); }
    void setInstanceName(const QString &name) { m_instanceName = name; }
    Cache cache() const { return m_cache; }
    void tick() { onTick(); }

signals:
    void changed();

private slots:
    void onFinished(const fortiq::license::LicenseClient::Result &result);
    void onTick();

private:
    void applyAnswer(const LicenseClient::Result &result);
    void setMessage(const QString &text);
    void persist();
    Decision decision() const;

    Config m_config;
    Policy m_policy;
    std::unique_ptr<LicenseStore> m_store;
    LicenseClient m_client;
    QTimer m_timer;
    bool m_required;
    QString m_key;
    Cache m_cache;
    bool m_busy = false;
    Kind m_pending = Kind::Validate;
    QString m_pendingKey;           // the key being activated (kept only until the answer arrives)
    QString m_message;
    QString m_instanceName;
    std::function<QDateTime()> m_now;
    std::function<bool(const QUrl &)> m_open;
    QDateTime m_lastSaved;
    int m_cleanup = 0;              // releases of activations made by mistake that are still on their way
};

}  // namespace fortiq::license
