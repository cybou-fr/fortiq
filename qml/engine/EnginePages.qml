import QtQuick

// The pages of a window: one EngineView per LIVE tab (the TabModel of the EngineBrowser decides which tabs are live), all of them sharing
// one profile. A tab that stops being live has its view destroyed, which frees the renderer; its address and title stay in the model and
// the page is loaded again when the tab is shown. Created by EngineWindow.qml for ONE gateway session (a new generation = new pages).
Item {
    id: pages
    required property var engine
    readonly property var tabs: engine.tabs
    property int revision: 0       // grows when a view is created/destroyed: `currentView` follows
    readonly property var currentView: {
        void pages.revision
        void tabs.activeId
        const loader = repeater.itemAt(tabs.activeIndex)
        return loader && loader.item ? loader.item : null
    }
    signal refused(string text)

    PageProfile { id: pageProfile; engine: pages.engine }

    Repeater {
        id: repeater
        model: pages.tabs
        delegate: Loader {
            id: slot
            required property int tabId
            required property bool tabLive
            required property bool tabCurrent
            objectName: "enginePage_" + tabId
            anchors.fill: pages
            visible: tabCurrent
            active: tabLive
            onItemChanged: pages.revision++
            sourceComponent: EngineView {
                id: v
                engine: pages.engine
                sharedProfile: pageProfile.profile
                url: "about:blank"
                property bool armed: false
                property int tabIdentity: slot.tabId
                function push() { if (armed) pages.tabs.report(slot.tabId, v.url.toString(), v.title, v.loading) }
                onUrlChanged: push()
                onTitleChanged: push()
                onLoadingChanged: push()
                onRefused: text => pages.refused(text)
                onNewTabRequested: address => pages.tabs.openFromOpener(slot.tabId, address)
                Component.onCompleted: {
                    // A page shown again after it was discarded (or a link opened as a tab) starts from the address the model kept.
                    const wanted = pages.tabs.urlOf(slot.tabId)
                    if (wanted.length > 0) v.url = wanted
                    armed = true
                }
            }
        }
    }
}
