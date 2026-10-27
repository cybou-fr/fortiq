#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace fortiq::engine {

// The tabs of the Browser engine (docs/BROWSER.md, "Onglets"). PURE logic (Qt Core only): which tabs exist, which one is shown, which of
// them hold a LIVE page and which were discarded to give their memory back. The web views themselves are QML (qml/engine/EnginePages.qml),
// one per live tab, driven by the `tabLive` role: a tab that stops being live has its view destroyed, which frees the renderer.
//   - Never zero tabs: closing the last one leaves a fresh blank tab (start page).
//   - maxTabs bounds how many tabs exist; liveLimit bounds how many hold a loaded page (desktop: a few, Android: ONE, because the system
//     WebView is a single native view). Beyond the live limit the least recently used background tab is discarded: it keeps its address and
//     title and is reloaded when shown again (scroll position and form input are lost; nothing is saved to disk).
//   - Nothing here is persisted. A new gateway session (another server, a restart) drops every tab (reset()).
class TabModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeChanged)
    Q_PROPERTY(int maxTabs READ maxTabs CONSTANT)
    Q_PROPERTY(int liveLimit READ liveLimit CONSTANT)
public:
    enum Role { IdRole = Qt::UserRole + 1, UrlRole, TitleRole, LoadingRole, LiveRole, CurrentRole, LabelRole, BlankRole };

    // Defaults of the platform (Android: 6 tabs, 1 live; desktop: 12 tabs, 4 live).
    static int defaultMaxTabs();
    static int defaultLiveLimit();
    explicit TabModel(QObject *parent = nullptr, int maxTabs = defaultMaxTabs(), int liveLimit = defaultLiveLimit());

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_tabs.size()); }
    int activeId() const { return m_active; }
    int activeIndex() const { return indexOfId(m_active); }
    int maxTabs() const { return m_maxTabs; }
    int liveLimit() const { return m_liveLimit; }
    int liveCount() const;

    // A new tab (blank, or loading `url`) after the active one; shown at once when `activate`. -1 + refused() when the limit is reached.
    Q_INVOKABLE int newTab(const QString &url = QString(), bool activate = true);
    // A link or popup that wants a new window: a tab next to its opener (never an external window). -1 + refused() at the limit.
    Q_INVOKABLE int openFromOpener(int openerId, const QString &url);
    Q_INVOKABLE void closeTab(int id);        // frees the page; the neighbour on the right (else left) is shown
    Q_INVOKABLE void activate(int id);        // shows the tab, makes it live (reloading a discarded one), discards the oldest background tab over the limit
    Q_INVOKABLE void activateNext(int step = 1);   // step -1 = previous; wraps around
    // A live view tells the model what it shows (title/address/loading). Unknown ids and discarded tabs are ignored.
    Q_INVOKABLE void report(int id, const QString &url, const QString &title, bool loading);
    Q_INVOKABLE void reset();                 // one fresh blank tab, everything else gone
    Q_INVOKABLE int indexOfId(int id) const;
    Q_INVOKABLE bool isLive(int id) const;
    Q_INVOKABLE QString urlOf(int id) const;  // the address a (re)created view must load; "" = start page

    static QString labelFor(const QString &url, const QString &title);

signals:
    void countChanged();
    void activeChanged();
    void refused(const QString &text);        // short French message for the window (tab limit)

private:
    struct Tab {
        int id = 0;
        QString url;      // "" = blank (start page); otherwise an http(s) address as last reported
        QString title;
        bool loading = false;
        bool live = false;
        quint64 used = 0; // recency (larger = more recent)
    };
    int add(const QString &url, bool activate, int afterIndex);
    void discardOldest(int keepRow, int room);
    void changedRow(int row, const QList<int> &roles = {});
    void freshBlank();

    QVector<Tab> m_tabs;
    int m_active = 0;
    int m_nextId = 1;
    quint64 m_clock = 0;
    int m_maxTabs;
    int m_liveLimit;
};

}  // namespace fortiq::engine
