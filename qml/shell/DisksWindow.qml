import QtQuick
import QtQuick.Controls.Fusion

Item {
    id:win
    objectName:"disksWindow"
    property bool adaptive:false
    property Item backdrop
    property var disks:null
    property var remote:null
    property string section:"mounts"
    property bool navigationOpen:false
    readonly property bool connected:remote!==null && remote.connected
    readonly property bool drawer:adaptive && width<900
    readonly property real contentLeft:drawer?0:196
    signal closeRequested()
    signal requestServers()
    signal openPath(string path)
    function sync(){if(disks)disks.setWindowActive(visible)}
    onVisibleChanged:sync()
    onDisksChanged:sync()
    Component.onCompleted:sync()
    function handleBack(){if(navigationOpen){navigationOpen=false;return true}return false}
    Glass { anchors.fill:parent;backdrop:win.backdrop;tint:Theme.appWindow;blurAmount:0.9;radius:16 }
    MouseArea { visible:win.drawer && win.navigationOpen;anchors.fill:parent;z:30;onClicked:win.navigationOpen=false }
    Rectangle {
        objectName:"disksSidebar"
        visible:!win.drawer || win.navigationOpen
        width:win.drawer?Math.min(260,win.width-32):196;height:parent.height;z:win.drawer?40:0
        topLeftRadius:16;bottomLeftRadius:16;color:win.drawer?"#F0121C30":"#40050B18"
        WindowDragArea { anchors.fill:parent;windowItem:win;movable:!win.adaptive }
        Row {
            visible:!win.adaptive;x:14;y:17;spacing:9
            Repeater { model:["#FF5F57","#FEBC2E","#28C840"]
                Rectangle { required property string modelData;width:12;height:12;radius:6;color:modelData
                    MouseArea { anchors.fill:parent;onClicked:if(parent.modelData==="#FF5F57")win.closeRequested() }
                }
            }
        }
        Text { x:18;y:48;text:"STOCKAGE";color:Theme.inkMuted;font.pixelSize:11;font.letterSpacing:1 }
        Column {
            x:8;y:72;width:parent.width-16;spacing:6
            Repeater { model:[{key:"mounts",title:"Volumes montés"},{key:"devices",title:"Périphériques"}]
                Rectangle {
                    required property var modelData
                    width:parent.width;height:win.adaptive?44:34;radius:8
                    color:win.section===modelData.key?"#D02D73DA":"transparent"
                    Text { x:12;anchors.verticalCenter:parent.verticalCenter;text:modelData.title;color:Theme.ink;font.pixelSize:13 }
                    MouseArea { anchors.fill:parent;onClicked:{win.section=modelData.key;win.navigationOpen=false} }
                }
            }
        }
        Text { x:18;y:190;width:parent.width-36;wrapMode:Text.WordWrap;text:"Serveur Linux sélectionné\nLecture seule";color:Theme.inkMuted;font.pixelSize:12 }
    }
    Item {
        x:win.contentLeft;width:parent.width-x;height:48
        WindowDragArea { anchors.fill:parent;windowItem:win;movable:!win.adaptive }
        ToolButtonGlass { visible:win.drawer;x:4;y:2;touch:true;glyph:"☰";onClicked:win.navigationOpen=!win.navigationOpen }
        Text { x:win.drawer?54:14;width:Math.max(0,parent.width-x-52);anchors.verticalCenter:parent.verticalCenter;text:"Disques · "+(win.connected?win.remote.serverLabel:"Aucun serveur");elide:Text.ElideMiddle;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:14 }
        ToolButtonGlass { anchors.right:parent.right;anchors.rightMargin:8;y:4;touch:win.adaptive;glyph:"↻";tip:"Actualiser";enabledState:win.connected && win.disks!==null && !win.disks.busy;onClicked:win.disks.reload() }
    }
    Column {
        x:win.contentLeft+16;y:58;width:parent.width-x-16;spacing:7
        Text { width:parent.width;text:win.section==="mounts"?"Volumes montés":"Disques et partitions";color:Theme.ink;font.pixelSize:18 }
        Text { width:parent.width;wrapMode:Text.WordWrap;textFormat:Text.PlainText;font.pixelSize:12;color:Theme.inkMuted;text:win.disks?win.disks.status:"Aucun serveur connecté" }
    }
    ListView {
        objectName:"disksList"
        x:win.contentLeft+12;y:112;width:parent.width-x-12;height:Math.max(0,parent.height-y-65)
        visible:win.connected && win.disks!==null
        clip:true;spacing:8
        model:win.disks?(win.section==="mounts"?win.disks.mounts:win.disks.devices):[]
        ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
        delegate:Rectangle {
            required property var modelData
            width:ListView.view.width-12;height:win.section==="mounts"?158:126;radius:10;color:"#30182740";border.width:1;border.color:"#25DCEBFF"
            Text { x:12;y:10;width:parent.width-24;text:win.section==="mounts"?modelData.target:modelData.path;elide:Text.ElideMiddle;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:14 }
            Text { x:12;y:33;width:parent.width-24;elide:Text.ElideMiddle;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:12;text:win.section==="mounts"?modelData.source+" · "+(modelData.fstype||"Type inconnu"):(modelData.type+" · "+(modelData.fstype||"Type FS inconnu")+(modelData.label?" · "+modelData.label:"")) }
            Text { x:12;y:55;width:parent.width-24;height:42;maximumLineCount:3;elide:Text.ElideRight;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:13;text:win.section==="mounts"?"Capacité : "+modelData.sizeText+"\nUtilisé : "+modelData.usedText+"\nDisponible : "+modelData.availText:"Capacité : "+modelData.sizeText+" · "+(!modelData.readOnlyKnown?"Mode inconnu":modelData.readOnly?"Lecture seule":"Lecture / écriture")+"\nParent : "+(modelData.pkname||"—")+" · Montage : "+(modelData.mountpoint||"Non monté / inconnu") }
            Rectangle { visible:win.section==="mounts" && modelData.fraction>=0;x:12;y:96;width:parent.width-24;height:5;radius:2;color:"#334F6788"
                Rectangle { width:parent.width*Math.max(0,modelData.fraction);height:parent.height;radius:2;color:modelData.fraction>=0.9?"#E5A65F":"#559CF2" }
            }
            ToolButtonGlass { objectName:"disksOpenFiles";visible:win.section==="mounts";x:10;y:107;glyph:"↗";touch:win.adaptive;tip:"Ouvrir dans Fichiers";onClicked:win.openPath(modelData.target) }
            Text { visible:win.section==="mounts";x:win.adaptive?60:46;y:117;text:"Ouvrir dans Fichiers";color:Theme.inkMuted;font.pixelSize:12 }
        }
    }
    Column {
        visible:!win.connected;x:win.contentLeft+16;y:150;width:parent.width-x-16;spacing:18
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;text:"Aucun serveur connecté";color:Theme.ink;font.pixelSize:16 }
        Button { objectName:"disksConnectButton";anchors.horizontalCenter:parent.horizontalCenter;text:"Choisir un serveur…";onClicked:win.requestServers() }
    }
    AppStateMessage {
        readonly property string sectionError: win.disks ? (win.section==="mounts"?win.disks.mountError:win.disks.deviceError) : ""
        visible:win.connected && win.disks!==null && !win.disks.busy && (win.section==="mounts"?win.disks.mounts.length:win.disks.devices.length)===0
        state: sectionError.length>0 ? "error" : "empty"
        touch:win.adaptive
        x:win.contentLeft+20;y:165;width:Math.max(0,parent.width-x-20)
        text:sectionError || "Aucune entrée signalée par cet outil."
    }
    Text { x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:10;width:parent.width-x-12;height:45;wrapMode:Text.WordWrap;maximumLineCount:3;elide:Text.ElideRight;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:11;text:win.disks?win.disks.versions+(win.disks.updated?"\nMis à jour : "+win.disks.updated:""):"lsblk / findmnt via SSH" }
    LinkBanner { remote: win.remote; touch: win.adaptive; onRetry: if (win.disks && !win.disks.busy) win.disks.reload() }
}
