#include "TabModel.h"

#include <QUrl>

#include <algorithm>

namespace fortiq::engine {

namespace {
constexpr int kMaxUrl = 2048;
constexpr int kMaxTitle = 200;
}  // namespace

int TabModel::defaultMaxTabs() {
#ifdef Q_OS_ANDROID
    return 6;
#else
    return 12;
#endif
}

int TabModel::defaultLiveLimit() {
#ifdef Q_OS_ANDROID
    return 1;
#else
    return 4;
#endif
}

TabModel::TabModel(QObject *parent, int maxTabs, int liveLimit)
    : QAbstractListModel(parent), m_maxTabs(std::max(1, maxTabs)), m_liveLimit(std::max(1, liveLimit)) {
    freshBlank();
}

void TabModel::freshBlank() {
    Tab t;
    t.id = m_nextId++;
    t.live = true;
    t.used = ++m_clock;
    m_tabs.clear();
    m_tabs.append(t);
    m_active = t.id;
}

int TabModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_tabs.size()); }

QHash<int, QByteArray> TabModel::roleNames() const {
    return { { IdRole, "tabId" }, { UrlRole, "tabUrl" }, { TitleRole, "tabTitle" }, { LoadingRole, "tabLoading" },
             { LiveRole, "tabLive" }, { CurrentRole, "tabCurrent" }, { LabelRole, "tabLabel" }, { BlankRole, "tabBlank" } };
}

QVariant TabModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tabs.size()) return {};
    const Tab &t = m_tabs.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case UrlRole: return t.url;
    case TitleRole: return t.title;
    case LoadingRole: return t.loading;
    case LiveRole: return t.live;
    case CurrentRole: return t.id == m_active;
    case LabelRole: return labelFor(t.url, t.title);
    case BlankRole: return t.url.isEmpty();
    }
    return {};
}

QString TabModel::labelFor(const QString &url, const QString &title) {
    const QString t = title.simplified();
    if (!t.isEmpty()) return t;
    if (url.isEmpty()) return QStringLiteral("Nouvel onglet");
    const QString host = QUrl(url).host();
    return host.isEmpty() ? url.left(40) : host;
}

int TabModel::indexOfId(int id) const {
    for (int i = 0; i < m_tabs.size(); ++i) if (m_tabs.at(i).id == id) return i;
    return -1;
}

bool TabModel::isLive(int id) const { const int i = indexOfId(id); return i >= 0 && m_tabs.at(i).live; }

QString TabModel::urlOf(int id) const { const int i = indexOfId(id); return i >= 0 ? m_tabs.at(i).url : QString(); }

int TabModel::liveCount() const {
    int n = 0;
    for (const Tab &t : m_tabs) n += t.live ? 1 : 0;
    return n;
}

void TabModel::changedRow(int row, const QList<int> &roles) {
    if (row >= 0 && row < m_tabs.size()) emit dataChanged(index(row), index(row), roles);
}

int TabModel::add(const QString &url, bool activateIt, int afterIndex) {
    if (m_tabs.size() >= m_maxTabs) {
        emit refused(QStringLiteral("Limite de %1 onglets atteinte : fermez un onglet pour en ouvrir un autre.").arg(m_maxTabs));
        return -1;
    }
    Tab t;
    t.id = m_nextId++;
    t.url = url.left(kMaxUrl);
    t.used = ++m_clock;
    const int row = std::clamp(afterIndex + 1, 0, int(m_tabs.size()));
    beginInsertRows({}, row, row);
    m_tabs.insert(row, t);
    endInsertRows();
    emit countChanged();
    if (activateIt) activate(t.id);
    return t.id;
}

int TabModel::newTab(const QString &url, bool activateIt) { return add(url, activateIt, activeIndex()); }

int TabModel::openFromOpener(int openerId, const QString &url) {
    const int opener = indexOfId(openerId);
    return add(url, true, opener >= 0 ? opener : activeIndex());
}

// Discards the least recently used live tabs (never `keepRow`) until `room` more pages fit under the live limit.
void TabModel::discardOldest(int keepRow, int room) {
    while (liveCount() + room > m_liveLimit) {
        int victim = -1;
        for (int i = 0; i < m_tabs.size(); ++i) {
            const Tab &t = m_tabs.at(i);
            if (!t.live || i == keepRow) continue;
            if (victim < 0 || t.used < m_tabs.at(victim).used) victim = i;
        }
        if (victim < 0) break;
        m_tabs[victim].live = false;
        m_tabs[victim].loading = false;
        changedRow(victim, { LiveRole, LoadingRole });
    }
}

void TabModel::activate(int id) {
    const int row = indexOfId(id);
    if (row < 0) return;
    const int old = indexOfId(m_active);
    m_tabs[row].used = ++m_clock;
    m_active = id;
    // Order matters for a single native view (Android): the page that goes is released BEFORE the one that comes is created.
    if (!m_tabs.at(row).live) {
        discardOldest(row, 1);
        m_tabs[row].live = true;
    }
    if (old >= 0 && old != row) changedRow(old, { CurrentRole });
    changedRow(row, { CurrentRole, LiveRole });
    emit activeChanged();
}

void TabModel::activateNext(int step) {
    if (m_tabs.isEmpty() || step == 0) return;
    const int n = int(m_tabs.size());
    const int row = ((activeIndex() + step) % n + n) % n;
    activate(m_tabs.at(row).id);
}

void TabModel::closeTab(int id) {
    const int row = indexOfId(id);
    if (row < 0) return;
    if (m_tabs.size() == 1) reset();   // the last tab: its page is freed, the window keeps a fresh blank one
    else {
        const bool wasActive = id == m_active;
        beginRemoveRows({}, row, row);
        m_tabs.removeAt(row);
        endRemoveRows();
        emit countChanged();
        if (wasActive) {
            const int next = std::min(row, int(m_tabs.size()) - 1);   // the right neighbour took this row; the last tab falls back to its left one
            m_active = 0;
            activate(m_tabs.at(next).id);
        }
    }
}

void TabModel::report(int id, const QString &url, const QString &title, bool loading) {
    const int row = indexOfId(id);
    if (row < 0 || !m_tabs.at(row).live) return;
    Tab &t = m_tabs[row];
    const QString u = url == QLatin1String("about:blank") ? QString() : url.left(kMaxUrl);
    const QString ti = title.left(kMaxTitle);
    QList<int> roles;
    if (t.url != u) { t.url = u; roles << UrlRole << BlankRole << LabelRole; }
    if (t.title != ti) { t.title = ti; roles << TitleRole << LabelRole; }
    if (t.loading != loading) { t.loading = loading; roles << LoadingRole; }
    if (!roles.isEmpty()) changedRow(row, roles);
}

void TabModel::reset() {
    beginResetModel();
    freshBlank();
    endResetModel();
    emit countChanged();
    emit activeChanged();
}

}  // namespace fortiq::engine
