import QtQuick
import QtWebEngine

// The ONE off-the-record profile shared by every tab of a window (a login made in one tab is seen by the others, as in any browser, and still
// lives only in memory: no cookie, cache, history or storage file on disk, ever). Created per gateway session (EnginePages.qml is reloaded
// with every new generation), destroyed with it. (Qt 6.9+ prefers WebEngineProfilePrototype.instance(); in 6.12 that call crashes inside
// QQuickWebEngineView::setProfile when the profile is assigned from QML, so the plain type is used; it only logs a deprecation warning.
// tests/EngineLiveTest.cpp checks the result, a Qt update that changes this is caught there.)
Item {
    id: holder
    required property var engine
    readonly property var profile: otr
    WebEngineProfile {
        id: otr
        offTheRecord: true
        httpCacheType: WebEngineProfile.MemoryHttpCache
        persistentCookiesPolicy: WebEngineProfile.NoPersistentCookies
        spellCheckEnabled: false
        onDownloadRequested: download => {
            download.cancel()
            holder.engine.reportDownloadRefused(download.downloadFileName)
        }
    }
}
