import QtQuick

// CONTROL (1215,64)→(1526,744). No real Wi-Fi/Bluetooth/brightness/media/
// agenda provider exists yet, so outside the fixture every tile is shown
// disabled with "indisponible", and the agenda says it has no source.
Glass {
    id: cc
    property bool fixture: false
    radius: 18
    tint: Theme.control

    component Tile: Rectangle {
        id: tile
        property string glyph: ""
        property string title: ""
        property string sub: ""
        property bool light: false
        property bool badge: false
        width: 138; height: 54; radius: 10
        color: light ? Theme.tileLight : Theme.tileDark
        border.width: 1; border.color: light ? "#80FFFFFF" : "#2EFFFFFF"
        opacity: cc.fixture ? 1 : 0.55
        Rectangle {
            visible: tile.badge
            x: 10; anchors.verticalCenter: parent.verticalCenter
            width: 38; height: 38; radius: 19; color: Theme.accentDeep
            SymbolGlyph { anchors.centerIn: parent; glyph: tile.glyph; color: "white"; size: 18 }
        }
        SymbolGlyph {
            visible: !tile.badge
            x: 14; anchors.verticalCenter: parent.verticalCenter
            glyph: tile.glyph; color: "white"; size: 18
        }
        Column {
            x: tile.badge ? 57 : 46; anchors.verticalCenter: parent.verticalCenter
            Text { text: tile.title; color: tile.light ? "#0E2A5C" : Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
            Text { visible: text.length > 0; text: cc.fixture ? tile.sub : "indisponible"; color: tile.light ? "#3B5684" : Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily }
        }
    }

    Tile { x: 14; y: 16; width: 138; height: 57; light: true; badge: true; glyph: "◠"; title: "Wi-Fi"; sub: "Livebox-5G" }
    Tile { x: 164; y: 16; width: 133; height: 57; light: true; badge: true; glyph: "ᛒ"; title: "Bluetooth"; sub: "Activé" }
    Tile { x: 14; y: 84; glyph: "☾"; title: "Mode sombre" }
    Tile { x: 164; y: 84; width: 133; glyph: "☾"; title: "Ne pas déranger" }
    Tile { x: 14; y: 146; glyph: "▯"; title: "Économiseur\nd'énergie" }
    Tile { x: 164; y: 146; width: 133; glyph: "✈"; title: "Mode avion" }

    Repeater {
        model: [ { g: "☼", y: 240, v: 0.67 }, { g: "🔊", y: 282, v: 0.73 } ]
        Item {
            required property var modelData
            x: 18; y: modelData.y; width: 280; height: 20
            opacity: cc.fixture ? 1 : 0.45
            Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.g; color: "white"; font.pixelSize: 16 }
            Rectangle {
                x: 47; anchors.verticalCenter: parent.verticalCenter
                width: 225; height: 6; radius: 3; color: "#40FFFFFF"
                Rectangle {
                    width: parent.width * modelData.v; height: parent.height; radius: 3
                    gradient: Gradient { orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#1F6FE5" }
                        GradientStop { position: 1; color: "#4FA3FF" } }
                }
                Rectangle {
                    x: parent.width * modelData.v - 9; anchors.verticalCenter: parent.verticalCenter
                    width: 18; height: 18; radius: 9; color: "white"
                }
            }
        }
    }

    Rectangle {
        x: 12; y: 316; width: 286; height: 146; radius: 14
        color: "#47192235"; border.width: 1; border.color: "#2EFFFFFF"
        Rectangle {
            x: 13; y: 13; width: 76; height: 76; radius: 8
            gradient: Gradient {
                GradientStop { position: 0; color: "#2C2A6E" }
                GradientStop { position: 0.55; color: "#D1477E" }
                GradientStop { position: 1; color: "#141428" }
            }
        }
        Text { x: 102; y: 20; text: cc.fixture ? "The Midnight" : "Lecteur multimédia"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
        Text { x: 102; y: 40; text: cc.fixture ? "Los Angeles" : "Aucune source serveur"; color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily }
        Rectangle {
            x: 102; y: 66; width: 168; height: 4; radius: 2; color: "#40FFFFFF"
            Rectangle { width: cc.fixture ? parent.width * 0.5 : 0; height: 4; radius: 2; color: Theme.accent }
        }
        Text { x: 102; y: 76; text: cc.fixture ? "01:24" : "--:--"; color: Theme.inkMuted; font.pixelSize: 11 }
        Text { x: 241; y: 76; text: cc.fixture ? "04:20" : "--:--"; color: Theme.inkMuted; font.pixelSize: 11 }
        Row {
            x: 70; y: 103; spacing: 50
            Repeater { model: ["⏮", "⏸", "⏭"]; Text { required property string modelData; text: modelData; color: "white"; font.pixelSize: 24 } }
        }
    }

    Rectangle { x: 0; y: 474; width: parent.width; height: 1; color: "#26FFFFFF" }
    Text { x: 22; y: 488; text: "Aujourd'hui"; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
    Rectangle {
        x: 260; y: 484; width: 30; height: 30; radius: 8; color: "#33FFFFFF"
        Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: 20 }
    }
    Text {
        visible: !cc.fixture
        x: 22; y: 530; width: 270; wrapMode: Text.WordWrap
        text: "Agenda non connecté : aucune source d'agenda sur le serveur."
        color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
    }
    Repeater {
        model: cc.fixture ? [
            { h: "14:00", t: "Réunion projet CYBOU", r: "14:00 – 15:00", c: "#2F8CFF" },
            { h: "16:00", t: "Appel Richard (AVISTO)", r: "16:00 – 17:00", c: "#B36BFF" },
            { h: "19:00", t: "Sport", r: "19:00 – 20:00", c: "#FF9A2E" }
        ] : []
        Item {
            required property var modelData
            required property int index
            x: 22; y: 524 + index * 51; width: 270; height: 42
            Text { y: 2; text: modelData.h; color: "white"; font.pixelSize: 12 }
            Rectangle { x: 59; width: 4; height: 36; radius: 2; color: modelData.c }
            Text { x: 79; y: 0; text: modelData.t; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
            Text { x: 79; y: 19; text: modelData.r; color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily }
        }
    }
}
