import QtQuick

// Blue square folder from the master Files window: back tab + front body,
// optional white emblem in the centre.
Item {
    id: folder
    property string emblem: ""
    implicitWidth: 56
    implicitHeight: 46

    Rectangle {
        x: 0; y: 0
        width: parent.width * 0.44
        height: parent.height * 0.3
        radius: parent.height * 0.08
        color: Theme.folderTab
    }
    Rectangle {
        y: parent.height * 0.12
        width: parent.width
        height: parent.height * 0.88
        radius: parent.height * 0.1
        color: Theme.folderTab
    }
    Rectangle {
        y: parent.height * 0.22
        width: parent.width
        height: parent.height * 0.78
        radius: parent.height * 0.1
        gradient: Gradient {
            GradientStop { position: 0; color: "#5AA6FF" }
            GradientStop { position: 1; color: Theme.folder }
        }
        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 1
            text: folder.emblem
            color: "#E8F2FF"
            font.pixelSize: parent.height * 0.48
            font.family: Theme.fontFamily
        }
    }
}
