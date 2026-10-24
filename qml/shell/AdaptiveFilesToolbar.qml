import QtQuick
import QtQuick.Controls.Fusion

// Touch-sized controls use the existing Files actions and dialogs.
Column {
    id: bar
    property var files
    spacing: 4
    padding: 4
    Row {
        visible: !Qt.inputMethod.visible
        spacing: 4
        ToolButtonGlass { visible: bar.files.drawer; touch: true; glyph: "☰"; tip: "Emplacements"; onClicked: bar.files.navigationOpen = !bar.files.navigationOpen }
        ToolButtonGlass { touch: true; glyph: "‹"; enabledState: bar.files.connected && bar.files.controller.canGoBack; onClicked: bar.files.controller.goBack() }
        ToolButtonGlass { touch: true; glyph: "↑"; enabledState: bar.files.connected; onClicked: bar.files.controller.goUp() }
        ToolButtonGlass { touch: true; glyph: "↻"; enabledState: bar.files.connected; onClicked: bar.files.controller.refresh() }
        ToolButtonGlass { touch: true; glyph: "⋯"; tip: "Actions"; onClicked: actions.popup(4, 48) }
        ToolButtonGlass { touch: true; glyph: "×"; tip: "Fermer"; onClicked: if (!bar.files.handleBack()) bar.files.closeRequested() }
    }
    TextField {
        visible: !Qt.inputMethod.visible || activeFocus
        width: bar.width - 8; height: 44
        text: bar.files.location
        placeholderText: "Chemin sur le serveur"
        selectByMouse: true; font.pixelSize: 14
        color: Theme.ink; placeholderTextColor: Theme.inkMuted; selectionColor: Theme.accent
        background: Rectangle { radius: 8; color: "#24FFFFFF"; border.width: 1; border.color: "#30FFFFFF" }
        onAccepted: if (bar.files.connected) { bar.files.controller.navigate(text); Qt.inputMethod.hide() }
    }
    TextField {
        visible: !Qt.inputMethod.visible || activeFocus
        width: bar.width - 8; height: 44
        text: bar.files.filterText
        placeholderText: "Filtrer · Entrée : chercher sur le serveur"
        selectByMouse: true; font.pixelSize: 14
        color: Theme.ink; placeholderTextColor: Theme.inkMuted; selectionColor: Theme.accent
        background: Rectangle { radius: 8; color: "#24FFFFFF"; border.width: 1; border.color: "#30FFFFFF" }
        onTextEdited: bar.files.filterText = text
        onAccepted: { bar.files.serverSearch("name"); Qt.inputMethod.hide() }
    }
    Menu {
        id: actions
        width: Math.min(360, bar.width - 8)
        palette.text: Theme.ink
        palette.windowText: Theme.ink
        palette.highlightedText: Theme.ink
        palette.highlight: Theme.selection
        background: Glass { backdrop: bar.files.backdrop; radius: 10; tint: "#ED101A2E" }
        MenuItem { text: "Ouvrir la sélection"; enabled: bar.files.selected !== null; height: 44; onTriggered: bar.files.open(bar.files.selected) }
        MenuItem { text: "Envoyer depuis cet appareil…"; enabled: bar.files.connected; height: 44; onTriggered: bar.files.pickUpload() }
        MenuItem { text: "Télécharger la sélection…"; enabled: bar.files.selected !== null && !bar.files.selected.directory; height: 44; onTriggered: bar.files.askDownload(bar.files.selected) }
        MenuItem { text: "Nouveau dossier…"; enabled: bar.files.connected; height: 44; onTriggered: bar.files.askNewFolder() }
        MenuItem { text: "Renommer…"; enabled: bar.files.selected !== null; height: 44; onTriggered: bar.files.askRename(bar.files.selected) }
        MenuItem { text: "Corbeille / supprimer…"; enabled: bar.files.selected !== null; height: 44; onTriggered: bar.files.askTrash(bar.files.selected) }
        MenuItem { text: "Chercher dans le contenu"; enabled: bar.files.filterText.length > 0; height: 44; onTriggered: bar.files.serverSearch("content") }
        MenuItem { text: "Fichiers cachés"; checkable: true; checked: bar.files.showHidden; height: 44; onTriggered: bar.files.showHidden = !bar.files.showHidden }
        MenuItem { text: "Vue en liste"; checkable: true; checked: bar.files.viewMode === "list"; height: 44; onTriggered: bar.files.viewMode = bar.files.viewMode === "list" ? "grid" : "list" }
    }
}
