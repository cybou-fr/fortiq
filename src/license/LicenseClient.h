#pragma once

#include "LicenseInfo.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

namespace fortiq::license {

// The License API of Lemon Squeezy over HTTPS: activate, validate, deactivate (form-encoded POST, JSON answer). This is the only direct
// network connection of the client besides SSH (AGENTS.md): no other host, no cookies, no redirects, no proxy credentials.
class LicenseClient : public QObject {
    Q_OBJECT
public:
    struct Result {
        Kind kind = Kind::Validate;
        Reply reply;                 // outcome Malformed when there was no usable JSON
        int httpStatus = 0;          // 0: no HTTP answer at all
        bool transient = false;      // network failure, timeout, 5xx, 429: says nothing about the licence, try again later
        QString transport;           // short text for the user when transient or malformed
    };

    explicit LicenseClient(QObject *parent = nullptr);
    // The single place the API address is decided. Anything but the real https API is refused unless the build is a testing build
    // (FORTIQ_LICENSE_TESTING) and the address is a loopback one: a released client talks to api.lemonsqueezy.com only.
    bool setBaseUrl(const QUrl &url);
    QUrl baseUrl() const { return m_base; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    void activate(const QString &key, const QString &instanceName);
    void validate(const QString &key, const QString &instanceId);
    void deactivate(const QString &key, const QString &instanceId);

signals:
    void finished(const fortiq::license::LicenseClient::Result &result);

private:
    void post(Kind kind, const QString &path, const QList<QPair<QString, QString>> &fields);
    QNetworkAccessManager m_network;
    QUrl m_base;
    int m_timeoutMs = 15000;
};

}  // namespace fortiq::license

Q_DECLARE_METATYPE(fortiq::license::LicenseClient::Result)
