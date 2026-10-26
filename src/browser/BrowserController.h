#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace fortiq {
class RemoteController;
class SshSession;
struct FetchOutcome;

// Browser = a GUI over curl on the selected Linux server (see docs/BROWSER.md).
// Requests leave from the server; this device only receives bytes over SSH and
// renders them without JavaScript, styles or any automatic resource loading.
class BrowserController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)          // idle | loading | confirm | ready | error | disconnected
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString url READ url NOTIFY changed)              // final address of the shown page
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)            // html | text | json | image | binary | error | empty
    Q_PROPERTY(QString contentHtml READ contentHtml NOTIFY changed)
    Q_PROPERTY(QString contentText READ contentText NOTIFY changed)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY changed)
    Q_PROPERTY(QVariantList headers READ headers NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QStringList notes READ notes NOTIFY changed)
    Q_PROPERTY(QString refreshUrl READ refreshUrl NOTIFY changed)
    Q_PROPERTY(bool reader READ reader WRITE setReader NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    // Address policy (docs/BROWSER.md): set while state == "confirm", i.e. the server resolved the address to an internal one.
    // {url, host, port, reason, details:[{ip,label,cls}], hop, from}. Nothing is fetched until the user answers.
    Q_PROPERTY(QVariantMap pendingConfirm READ pendingConfirm NOTIFY changed)

public:
    explicit BrowserController(RemoteController *remote, QObject *parent = nullptr);
    ~BrowserController() override;

    bool busy() const { return m_busy; }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString url() const { return m_url; }
    QString title() const { return m_title; }
    QString kind() const { return m_kind; }
    QString contentHtml() const { return m_html; }
    QString contentText() const { return m_text; }
    QString imageSource() const { return m_image; }
    QVariantList headers() const { return m_headers; }
    QVariantMap info() const { return m_info; }
    QStringList notes() const { return m_notes; }
    QString refreshUrl() const { return m_refreshUrl; }
    bool reader() const { return m_reader; }
    void setReader(bool on);
    QVariantMap pendingConfirm() const { return m_pendingMap; }
    bool canGoBack() const { return m_index > 0; }
    bool canGoForward() const { return m_index >= 0 && m_index + 1 < m_history.size(); }

    // What the user typed in the address bar.
    Q_INVOKABLE void open(const QString &text);
    // A link clicked in the page (resolved against the current address; unsafe targets are ignored).
    Q_INVOKABLE void openLink(const QString &href);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    // The user's answer to an internal-address question: open it this once (this host and port, for this navigation only) or not.
    Q_INVOKABLE void confirmInternal();
    Q_INVOKABLE void cancelConfirm();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void setWindowActive(bool active);

signals:
    void changed();

private:
    struct Chain;   // one navigation: where it stands, what was followed, what the user allowed
    void navigate(const QString &url, bool record);
    void runChain(const Chain &chain);
    void onChain(const FetchOutcome &outcome, const Chain &chain);
    void apply(const FetchOutcome &outcome, int seq, int token, const QString &requested, bool record, int taskId);
    void clearPage(const QString &state, const QString &status);
    void onRemoteChanged();

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    std::shared_ptr<std::atomic_bool> m_cancel;
    int m_seq = 0, m_token = -1, m_taskId = 0;
    int m_readerGeneration = 0;   // newest reader-mode toggle; older sanitizing results are dropped
    bool m_busy = false, m_active = false, m_reader = true;
    QString m_state = QStringLiteral("disconnected");
    QString m_status = QStringLiteral("Aucun serveur connecté");
    QString m_url, m_title, m_kind = QStringLiteral("empty"), m_html, m_text, m_image, m_refreshUrl;
    QString m_requested;       // address being (or last) requested
    QVariantList m_headers;
    QVariantMap m_info;
    QStringList m_notes;
    QVariantMap m_pendingMap;
    std::shared_ptr<Chain> m_pending;   // the navigation waiting for the user's answer
    QStringList m_history;
    int m_index = -1;
    // Raw page kept in RAM so that reader mode can be toggled without a new request.
    QString m_rawHtml;
};
}  // namespace fortiq
