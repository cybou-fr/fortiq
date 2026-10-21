import QtQuick

// Original FORTIQ app glyphs drawn from primitives: no third-party logos.
// `kind` selects the artwork; unknown kinds fall back to a neutral tile.
Item {
    id: icon
    property string kind: "files"
    property real size: 44
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    readonly property var artwork: ({
        files:    ["#5FB4FF", "#1F6FE5", ""],
        browser:  ["#FFB347", "#E8432E", "◉"],
        terminal: ["#3A4150", "#14181F", ">_"],
        editor:   ["#5AB0FF", "#1B6FD8", "</>"],
        docs:     ["#6FB2FF", "#2F7BEA", "≡"],
        graphics: ["#FFD34E", "#9B5CF6", "✦"],
        media:    ["#FF5B6E", "#D9233F", "▶"],
        settings: ["#7C8798", "#4A5465", "⚙"],
        logs:     ["#4FD1C5", "#2C7A9B", "☰"],
        sheet:    ["#3FCB7A", "#16884A", "▦"],
        office:   ["#43D17F", "#1E9E55", ""],
        cad:      ["#FFAA45", "#E2671F", "◎"],
        vector:   ["#5B6270", "#23272F", "✒"],
        cone:     ["#FFA23A", "#F06A1A", "▲"],
        music:    ["#2EDB6E", "#139A47", "≋"],
        chat:     ["#7B83FF", "#4950D8", "T"],
        game:     ["#2C3E5A", "#0F1A2B", "◐"],
        paint:    ["#9A7B5A", "#5A4128", "✎"],
        apps:     ["#00000000", "#00000000", "⋮⋮"],
        trash:    ["#C9D6E8", "#8FA3BE", "🗑"],
        add:      ["#4A5266", "#2C3242", "+"]
    })
    readonly property var colors: artwork[kind] !== undefined ? artwork[kind] : ["#6B7A90", "#3B4658", "?"]

    Rectangle {
        anchors.fill: parent
        radius: icon.size * 0.24
        visible: icon.kind !== "files" && icon.kind !== "apps"
        gradient: Gradient {
            GradientStop { position: 0; color: icon.colors[0] }
            GradientStop { position: 1; color: icon.colors[1] }
        }
        Text {
            anchors.centerIn: parent
            text: icon.colors[2]
            color: "white"
            font.pixelSize: icon.size * (text.length > 1 ? 0.36 : 0.52)
            font.bold: true
            font.family: Theme.fontFamily
        }
    }

    FolderGlyph {
        visible: icon.kind === "files"
        anchors.centerIn: parent
        width: icon.size
        height: icon.size * 0.82
    }

    Grid {
        visible: icon.kind === "apps"
        anchors.centerIn: parent
        columns: 3
        spacing: icon.size * 0.12
        Repeater {
            model: 9
            Rectangle { width: icon.size * 0.11; height: width; radius: width / 2; color: "white" }
        }
    }
}
