#include "LicenseClient.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace fortiq::license {

LicenseClient::LicenseClient(QObject *parent) : QObject(parent), m_base(QStringLiteral("https://api.lemonsqueezy.com")) {
    qRegisterMetaType<fortiq::license::LicenseClient::Result>();
    m_network.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);   // the API does not redirect; a redirect is never followed
}

bool LicenseClient::setBaseUrl(const QUrl &url) {
#ifdef FORTIQ_LICENSE_TESTING
    const bool loopback = url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost");
    if (url.isValid() && loopback && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))) { m_base = url; return true; }
#endif
    if (url.isValid() && url.scheme() == QLatin1String("https") && url.host() == QLatin1String("api.lemonsqueezy.com") && url.port() == -1) { m_base = url; return true; }
    return false;
}

void LicenseClient::activate(const QString &key, const QString &instanceName) {
    post(Kind::Activate, QStringLiteral("/v1/licenses/activate"), { { QStringLiteral("license_key"), key }, { QStringLiteral("instance_name"), instanceName } });
}
void LicenseClient::validate(const QString &key, const QString &instanceId) {
    QList<QPair<QString, QString>> fields = { { QStringLiteral("license_key"), key } };
    if (!instanceId.isEmpty()) fields.append({ QStringLiteral("instance_id"), instanceId });
    post(Kind::Validate, QStringLiteral("/v1/licenses/validate"), fields);
}
void LicenseClient::deactivate(const QString &key, const QString &instanceId) {
    post(Kind::Deactivate, QStringLiteral("/v1/licenses/deactivate"), { { QStringLiteral("license_key"), key }, { QStringLiteral("instance_id"), instanceId } });
}

void LicenseClient::post(Kind kind, const QString &path, const QList<QPair<QString, QString>> &fields) {
    QUrl url = m_base;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FORTIQ/%1").arg(QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("0") : QCoreApplication::applicationVersion()));
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QByteArray body;                                        // the key travels in the body, never in the address
    for (const auto &f : fields) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(f.first) + '=' + QUrl::toPercentEncoding(f.second);
    }
    QNetworkReply *reply = m_network.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, kind] {
        reply->deleteLater();
        Result result;
        result.kind = kind;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->read(1 << 20);
        result.reply = parseReply(kind, body);
        const bool noAnswer = result.httpStatus == 0;
        if (noAnswer) {
            result.transient = true;
            result.transport = reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError
                ? QStringLiteral("Le serveur de licences ne répond pas (délai dépassé).")
                : QStringLiteral("Le serveur de licences est injoignable (%1).").arg(reply->errorString().left(120));
        } else if (result.httpStatus >= 500 || result.httpStatus == 429 || result.httpStatus == 408) {
            result.transient = true;
            result.transport = result.httpStatus == 429 ? QStringLiteral("Trop de demandes au serveur de licences : réessayez dans un instant.")
                                                         : QStringLiteral("Le serveur de licences est momentanément indisponible (HTTP %1).").arg(result.httpStatus);
        } else if (result.httpStatus >= 300 && result.httpStatus < 400) {
            result.transient = true;
            result.transport = QStringLiteral("Réponse inattendue du serveur de licences (redirection).");
        } else if (result.reply.outcome == Reply::Outcome::Malformed) {
            result.transport = QStringLiteral("Réponse inattendue du serveur de licences (HTTP %1).").arg(result.httpStatus);
        }
        emit finished(result);
    });
}

}  // namespace fortiq::license
