import QtQuick

// CLOCK (119,70)→(361,275). Client-local time. Weather needs a permitted
// server-side provider over SSH; until then it is either the labelled
// reference sample (fixture) or shown as unavailable.
Item {
    id: clock
    property string timeText: "13:45"
    property string dateText: "Mercredi 18 octobre"
    property bool sampleWeather: false
    width: 260; height: 210

    Text {
        x: 0; y: -6
        text: clock.timeText
        color: "white"
        font.pixelSize: 64; font.weight: Font.Light; font.family: Theme.fontFamily
    }
    Text {
        x: 2; y: 64
        text: clock.dateText
        color: "white"
        font.pixelSize: 19; font.family: Theme.fontFamily
    }

    Item {
        visible: clock.sampleWeather
        y: 112
        Rectangle { x: 6; y: 8; width: 30; height: 30; radius: 15; color: "#FFC93C" }
        Text { x: 52; y: 0; text: "22°"; color: "white"; font.pixelSize: 30; font.weight: Font.Light; font.family: Theme.fontFamily }
        Text { x: 54; y: 36; text: "Perpignan"; color: "#EAF0FA"; font.pixelSize: 16; font.family: Theme.fontFamily }
        Text { x: 54; y: 58; text: "Ensoleillé"; color: "#D0DAEA"; font.pixelSize: 11; font.family: Theme.fontFamily }
        Text { x: 54; y: 74; text: "↑ 24°  ↓ 12°"; color: "#D0DAEA"; font.pixelSize: 11; font.family: Theme.fontFamily }
    }
    Text {
        visible: !clock.sampleWeather
        x: 2; y: 112
        text: "Météo indisponible — aucun fournisseur serveur"
        color: "#C9D6EA"
        font.pixelSize: 12; font.family: Theme.fontFamily
    }
}
