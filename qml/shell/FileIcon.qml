import QtQuick

// Type icon for a remote entry: blue folder, or a page tinted by category
// with its extension printed on it. Category comes from the name only; the
// file content is never fetched for an icon.
Item {
    id: icon
    property string name: ""
    property bool directory: false
    property bool link: false
    property real size: 48
    width: size
    height: size

    readonly property string ext: {
        const i = name.lastIndexOf(".")
        return i > 0 && i < name.length - 1 ? name.slice(i + 1).toLowerCase() : ""
    }
    readonly property var categories: ({
        image: ["png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "ico", "tif", "tiff"],
        video: ["mp4", "mkv", "webm", "avi", "mov"],
        audio: ["mp3", "flac", "ogg", "wav", "m4a", "opus"],
        archive: ["zip", "tar", "gz", "tgz", "xz", "bz2", "zst", "7z", "rar", "deb", "rpm"],
        code: ["c", "h", "cpp", "hpp", "py", "js", "ts", "go", "rs", "java", "sh", "qml", "rb", "php", "lua", "sql"],
        config: ["conf", "cfg", "ini", "yaml", "yml", "toml", "json", "xml", "env", "service", "timer"],
        log: ["log", "journal", "1", "2", "old"],
        doc: ["pdf", "doc", "docx", "odt", "rtf", "md", "txt", "csv", "xls", "xlsx", "ods", "ppt", "pptx", "odp"]
    })
    readonly property string category: {
        for (const key in categories)
            if (categories[key].indexOf(ext) >= 0) return key
        return name.startsWith(".") ? "config" : "file"
    }
    readonly property var tints: ({
        image: "#B65CF6", video: "#F0506E", audio: "#2EC77A", archive: "#C98A3A",
        code: "#3FA2FF", config: "#8C9AB3", log: "#F2A93B", doc: "#4F8DF7", file: "#A9B6CC"
    })

    FolderGlyph {
        visible: icon.directory
        anchors.centerIn: parent
        width: icon.size
        height: icon.size * 0.82
    }

    Item {
        visible: !icon.directory
        anchors.centerIn: parent
        width: icon.size * 0.76
        height: icon.size * 0.94
        Rectangle {
            anchors.fill: parent
            radius: icon.size * 0.08
            color: "#EAF1FB"
            border.width: 1
            border.color: "#40FFFFFF"
        }
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: parent.height * 0.36
            bottomLeftRadius: icon.size * 0.08; bottomRightRadius: icon.size * 0.08
            color: icon.tints[icon.category]
            Text {
                anchors.centerIn: parent
                width: parent.width - 4
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: icon.ext.length > 0 ? icon.ext.toUpperCase() : "•••"
                color: "white"
                font.pixelSize: Math.max(7, icon.size * 0.17)
                font.bold: true
                font.family: Theme.fontFamily
            }
        }
        Column {
            x: parent.width * 0.16; y: parent.height * 0.14; spacing: icon.size * 0.06
            Repeater {
                model: 3
                Rectangle { width: icon.size * (index === 2 ? 0.28 : 0.44); height: Math.max(1, icon.size * 0.035); radius: height / 2; color: "#9FB0C8" }
            }
        }
    }

    Rectangle {
        visible: icon.link
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: icon.size * 0.34; height: width; radius: width / 2
        color: "#0E1A2E"; border.width: 1; border.color: "#80FFFFFF"
        Text { anchors.centerIn: parent; text: "↗"; color: "white"; font.pixelSize: parent.width * 0.62 }
    }
}
