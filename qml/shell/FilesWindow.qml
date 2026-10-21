import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// FILES (471,72)→(1189,475): dark glass sidebar with traffic-light controls
// at its upper-left, light toolbar, white 4-column grid. Real entries come
// from `controller` (RemoteController); `fixture` substitutes the labelled
// reference sample and must never be used as production data.
Item {
    id: win
    property Item backdrop
    property var controller: null
    property bool fixture: false
    signal closeRequested()

    readonly property bool connected: controller !== null && controller.connected
    readonly property var sampleEntries: [
        { name: "Bureau", emblem: "", directory: true },
        { name: "Documents", emblem: "▤", directory: true },
        { name: "Images", emblem: "▧", directory: true },
        { name: "Vidéos", emblem: "▦", directory: true },
        { name: "Musique", emblem: "♫", directory: true },
        { name: "Téléchargements", emblem: "⤓", directory: true },
        { name: "Projets", emblem: "", directory: true },
        { name: "Travail", emblem: "", directory: true },
        { name: "Montagne.jpg", file: "image" },
        { name: "Présentation.pdf", file: "pdf" },
        { name: "Budget.xlsx", file: "sheet" },
        { name: "Notes.txt", file: "text" }
    ]
    readonly property var entries: fixture ? sampleEntries : (connected ? controller.entries : [])
    readonly property string location: fixture ? "Dossier personnel"
        : (connected ? controller.currentDirectory : "Non connecté")

    RectangularShadow {
        anchors.fill: parent
        offset.y: 12; radius: 16; blur: 40
        color: "#66000814"
    }

    Item {
        id: body
        anchors.fill: parent

        Glass {
            id: sidebar
            x: 0; y: 0
            width: 173; height: parent.height + 27
            backdrop: win.backdrop
            radius: 16
            shadow: false
            tint: Theme.filesSidebar
            edgeColor: "transparent"

            Row {
                x: 14; y: 17; spacing: 9
                Repeater {
                    model: ["#FF5F57", "#FEBC2E", "#28C840"]
                    Rectangle {
                        required property string modelData
                        width: 12; height: 12; radius: 6; color: modelData
                        MouseArea { anchors.fill: parent; onClicked: if (parent.modelData === "#FF5F57") win.closeRequested() }
                    }
                }
            }

            Column {
                x: 8; y: 46; width: 141
                Repeater {
                    model: [
                        { g: "⌂", t: "Dossier personnel", gap: 0 },
                        { g: "◷", t: "Récents", gap: 0 },
                        { g: "★", t: "Favoris", gap: 0 },
                        { g: "▤", t: "Documents", gap: 0 },
                        { g: "▧", t: "Images", gap: 0 },
                        { g: "▶", t: "Vidéos", gap: 0 },
                        { g: "♫", t: "Musique", gap: 0 },
                        { g: "⤓", t: "Téléchargements", gap: 0 },
                        { g: "🗑", t: "Corbeille", gap: 18 },
                        { g: "▭", t: "Projets", gap: 0 },
                        { g: "▭", t: "Travail", gap: 0 },
                        { g: "▭", t: "Personnel", gap: 0 }
                    ]
                    Item {
                        required property var modelData
                        required property int index
                        width: 141
                        height: 30 + modelData.gap
                        Rectangle {
                            visible: index === 0
                            width: parent.width; height: 30; radius: 6
                            color: Theme.selection
                        }
                        Text { x: 10; y: 6; text: modelData.g; color: "white"; font.pixelSize: 13 }
                        Text {
                            x: 33; y: 7
                            text: modelData.t
                            color: Theme.ink
                            font.pixelSize: 12; font.family: Theme.fontFamily
                        }
                        Rectangle {
                            visible: modelData.gap > 0
                            x: 8; y: 30 + modelData.gap / 2
                            width: parent.width - 16; height: 1
                            color: "#3DFFFFFF"
                        }
                    }
                }
            }
        }

        Rectangle {
            id: paper
            x: 157; width: parent.width - x; height: parent.height
            color: Theme.filesBody
            topRightRadius: 16; bottomRightRadius: 16
        }

        Rectangle {
            id: toolbar
            x: paper.x; width: paper.width; height: 46
            color: Theme.filesToolbar
            topRightRadius: 16
            Row {
                x: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 18
                Text { text: "‹"; font.pixelSize: 22; color: "#2A3140"
                    MouseArea { anchors.fill: parent; enabled: win.connected; onClicked: win.controller.goUp() } }
                Text { text: "›"; font.pixelSize: 22; color: "#9AA3B2" }
            }
            Text {
                x: 72; anchors.verticalCenter: parent.verticalCenter
                width: 360; elide: Text.ElideMiddle
                text: "⌂ " + win.location
                color: Theme.paperInk
                font.pixelSize: 12; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Rectangle {
                x: 416; anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; radius: 7; color: "#E3E7EF"
                Text { anchors.centerIn: parent; text: "⌕"; font.pixelSize: 16; color: "#2A3140" }
            }
            Row {
                x: 462; anchors.verticalCenter: parent.verticalCenter; spacing: 18
                Repeater {
                    model: ["▦", "⇡", "⏻"]
                    Text { required property string modelData; text: modelData; font.pixelSize: 14; color: "#2A3140" }
                }
            }
        }

        GridView {
            id: grid
            visible: win.fixture || win.connected
            x: paper.x + 16; y: toolbar.height + 18
            width: parent.width - x - 16; height: parent.height - y
            cellWidth: width / 4; cellHeight: 107
            clip: true
            interactive: !win.fixture
            model: win.entries
            delegate: Item {
                required property var modelData
                width: grid.cellWidth; height: grid.cellHeight
                readonly property bool isDir: modelData.directory === true
                FolderGlyph {
                    visible: isDir
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6; width: 56; height: 46
                    emblem: modelData.emblem !== undefined ? modelData.emblem : ""
                }
                FileThumb {
                    visible: !isDir
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: modelData.file === "image" ? 9 : -2
                    kind: modelData.file !== undefined ? modelData.file : "text"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: isDir ? 61 : 76
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: modelData.name
                    color: Theme.paperInk
                    font.pixelSize: 12; font.family: Theme.fontFamily
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !win.fixture && isDir && win.connected && !win.controller.busy
                    onDoubleClicked: win.controller.openDirectory(modelData.path)
                }
            }
        }

        ConnectPane {
            visible: !win.fixture && !win.connected
            x: paper.x + 24; y: toolbar.height + 16
            width: parent.width - x - 24; height: parent.height - y - 16
            controller: win.controller
        }
    }
}
