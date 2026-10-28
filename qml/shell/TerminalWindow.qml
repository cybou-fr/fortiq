import QtQuick
import QtQuick.Controls.Fusion

Item {
    id: win
    objectName: "terminalWindow"
    property bool adaptive: false
    property Item backdrop
    property var terminal: null
    property var remote: null
    property string pasteText: ""
    property bool navigationOpen: false
    property bool searchOpen: false
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 210
    signal closeRequested()
    signal requestServers()
    // A command proposed by another app ("Terminal ici"): shown in full, typed after the user confirms and never run: no line break is sent, Enter is the user's.
    function proposeCommand(command) { pasteText = command; pasteDialog.open(); return true }
    function syncSession() { if(terminal)terminal.setWindowActive(visible) }
    function openSearch() { searchOpen=true;navigationOpen=false;searchInput.forceActiveFocus();searchInput.selectAll() }
    function closeSearch() { searchDelay.stop();searchOpen=false;surface.clearSearch();surface.forceActiveFocus() }
    onVisibleChanged: syncSession()
    onTerminalChanged: syncSession()
    Component.onCompleted: syncSession()
    component Action: Rectangle {
        property string text: ""
        signal clicked()
        width: caption.implicitWidth + 22
        height: win.adaptive ? 40 : 30
        radius: 8
        color: enabled ? (actionMouse.containsMouse ? "#663E80C4" : "#44395880") : "#221E304A"
        border.width: 1; border.color: "#33DCEBFF"
        opacity: enabled ? 1 : 0.45
        Text { id: caption; anchors.centerIn: parent; text: parent.text; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
        MouseArea { id: actionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }
    function handleBack() {
        if(navigationOpen){navigationOpen=false;return true}
        if(pasteDialog.visible){pasteDialog.close();return true}
        if(searchOpen){closeSearch();return true}
        return false
    }
    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }
    MouseArea { visible:win.drawer && win.navigationOpen;anchors.fill:parent;z:30;onClicked:win.navigationOpen=false }
    Rectangle {
        id: sidebar
        objectName: "terminalSidebar"
        visible: !win.drawer || win.navigationOpen
        width: win.drawer ? Math.min(260,win.width-32) : 210
        height: parent.height
        z: win.drawer ? 40 : 0
        topLeftRadius: 16; bottomLeftRadius: 16
        color: win.drawer ? "#F0121C30" : "#40050B18"
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#1FFFFFFF" }
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        Row {
            visible: !win.adaptive
            x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if(parent.modelData==="#FF5F57")win.closeRequested() }
                }
            }
        }
        Text { x:18;y:48;text:"HISTORIQUE BASH";color:"#8FA2C2";font.pixelSize:10;font.letterSpacing:1;font.family:Theme.fontFamily }
        Text {
            x:18;y:70;width:parent.width-36;height:54;wrapMode:Text.WordWrap;maximumLineCount:3;elide:Text.ElideRight
            text: win.terminal ? win.terminal.historyStatus : "Historique enregistré sur le serveur."
            color:Theme.inkMuted;font.pixelSize:10;font.family:Theme.fontFamily;textFormat:Text.PlainText
        }
        ListView {
            objectName: "terminalHistory"
            x:8;y:130;width:parent.width-16;height:Math.max(0,parent.height-y-12)
            clip:true;spacing:1;interactive:count>0
            model:win.terminal ? win.terminal.history : []
            ScrollBar.vertical: ScrollBar { policy:ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width:ListView.view.width;height:Math.max(win.adaptive ? 40 : 24,entry.implicitHeight+8);radius:5
                color:historyMouse.containsMouse ? "#443E80C4" : "#181B3658"
                Text {
                    id:entry
                    x:10;anchors.verticalCenter:parent.verticalCenter;width:parent.width-20
                    text:modelData.text;textFormat:Text.PlainText
                    wrapMode:Text.WrapAnywhere;maximumLineCount:2;elide:Text.ElideRight
                    color:Theme.ink;font.pixelSize:11;font.family:Theme.fontFamily
                }
                MouseArea {
                    id:historyMouse;anchors.fill:parent;hoverEnabled:true;enabled:surface.inputEnabled
                    onClicked:{win.navigationOpen=false;surface.insertHistory(modelData.text.replace(/\t/g," "))}
                }
            }
        }
    }
    Item {
        id: toolbar
        x:win.contentLeft;width:parent.width-x;height:48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible:win.drawer;x:4;y:2;touch:true;glyph:"☰";onClicked:win.navigationOpen=!win.navigationOpen }
        Text {
            x:win.drawer?54:14;anchors.verticalCenter:parent.verticalCenter;width:Math.max(0,parent.width-actions.width-x-20);elide:Text.ElideMiddle
            text:"Terminal · "+(win.connected?win.remote.serverLabel:"Aucun serveur")
            color:Theme.ink;font.pixelSize:14;font.family:Theme.fontFamily
        }
        Row {
            id:actions
            anchors.right:parent.right;anchors.rightMargin:8;anchors.verticalCenter:parent.verticalCenter;spacing:4
            Action { text:"Copier";enabled:surface.visible;onClicked:surface.copyScreen() }
            Action { text:"Coller…";enabled:surface.inputEnabled;onClicked:surface.pasteRequested() }
            Action { objectName:"terminalSearchButton";text:"⌕";enabled:surface.visible;onClicked:win.openSearch() }
        }
    }
    Item {
        id:searchBar;objectName:"terminalSearchBar"
        visible:win.searchOpen
        x:win.contentLeft+12;y:50;width:win.width-x-12;height:win.adaptive?44:36
        TextField {
            id:searchInput;objectName:"terminalSearchInput"
            width:Math.max(40,parent.width-searchActions.width-8);height:parent.height
            placeholderText:"Rechercher dans la sortie…";selectByMouse:true;maximumLength:256
            onTextEdited:searchDelay.restart()
            Keys.onPressed:event=>{
                if(event.key===Qt.Key_Escape){event.accepted=true}
                else if(event.key===Qt.Key_Return || event.key===Qt.Key_Enter){searchDelay.stop();surface.findText(text,!!(event.modifiers&Qt.ShiftModifier));event.accepted=true}
            }
            Keys.onReleased:event=>{if(event.key===Qt.Key_Escape){event.accepted=true;win.closeSearch()}}
        }
        Timer { id:searchDelay;interval:180;onTriggered:surface.findText(searchInput.text,false) }
        Row {
            id:searchActions;anchors.right:parent.right;spacing:4
            ToolButtonGlass { glyph:"↑";touch:win.adaptive;enabled:searchInput.text.length>0;onClicked:{searchDelay.stop();surface.findText(searchInput.text,true)} }
            ToolButtonGlass { glyph:"↓";touch:win.adaptive;enabled:searchInput.text.length>0;onClicked:{searchDelay.stop();surface.findText(searchInput.text,false)} }
            ToolButtonGlass { glyph:"×";touch:win.adaptive;onClicked:win.closeSearch() }
        }
    }
    TerminalView {
        id:surface
        objectName:"terminalSurface"
        x:win.contentLeft+12;y:52+(win.searchOpen?searchBar.height+4:0);width:Math.max(32,parent.width-x-(win.adaptive?32:24));height:Math.max(32,parent.height-y-28)
        visible:win.connected && win.terminal!==null && win.terminal.running
        inputEnabled:win.terminal!==null && win.terminal.running
        onBytes:data=>{if(win.terminal)win.terminal.send(data)}
        onGridChanged:(columns,rows)=>{if(win.terminal)win.terminal.resize(columns,rows)}
        onPasteRequested:{win.pasteText=clipboardText();pasteDialog.open()}
        onSearchRequested:win.openSearch()
        Component.onCompleted:if(win.terminal)win.terminal.resize(columns,rows)
    }
    ScrollBar {
        id:outputScroll
        objectName:"terminalOutputScroll"
        visible:surface.visible && surface.scrollbackLines>0
        x:win.width-width-2;y:surface.y;height:surface.height;width:win.adaptive?28:18
        orientation:Qt.Vertical;policy:ScrollBar.AlwaysOn;active:true
        size:surface.rows/(surface.scrollbackLines+surface.rows)
        minimumSize:Math.min(1,40/Math.max(1,height))
        stepSize:1/Math.max(1,surface.scrollbackLines+surface.rows)
        Binding {
            target:outputScroll;property:"position";when:!outputScroll.pressed
            value:(surface.scrollbackLines-surface.scrollOffset)/(surface.scrollbackLines+surface.rows)
            restoreMode:Binding.RestoreNone
        }
        onPositionChanged:if(pressed)surface.scrollTo(Math.round(surface.scrollbackLines-position*(surface.scrollbackLines+surface.rows)))
    }
    Item {
        visible:!surface.visible
        x:win.contentLeft;y:48;width:parent.width-x;height:parent.height-y-28
        Column {
            anchors.centerIn:parent;width:Math.max(40,parent.width-32);spacing:16
            Text { width:parent.width;text:win.connected?(win.terminal?win.terminal.status:"Terminal indisponible."):"Aucun serveur connecté";textFormat:Text.PlainText;wrapMode:Text.WordWrap;horizontalAlignment:Text.AlignHCenter;color:Theme.ink;font.pixelSize:15;font.family:Theme.fontFamily }
            Text { visible:!win.connected;width:parent.width;text:"Le terminal utilise le serveur Linux choisi.";horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;color:Theme.inkMuted;font.pixelSize:12;font.family:Theme.fontFamily }
            Action {
                objectName:"terminalConnectButton"
                anchors.horizontalCenter:parent.horizontalCenter
                visible:!win.connected || (win.terminal!==null && !win.terminal.busy)
                text:win.connected?"Nouvelle session":"Choisir un serveur…"
                onClicked:{if(!win.connected)win.requestServers();else win.terminal.start()}
            }
        }
    }
    property bool outputThrottled:false
    Connections {
        target:surface
        // Backpressure with hysteresis: stop reading from the server while more than 256 KiB of output waits to be drawn, resume below 64 KiB.
        function onBacklogChanged(){
            if(!win.terminal)return
            if(!win.outputThrottled && surface.backlog>262144){win.outputThrottled=true;win.terminal.setBackpressure(true)}
            else if(win.outputThrottled && surface.backlog<65536){win.outputThrottled=false;win.terminal.setBackpressure(false)}
        }
    }
    Connections {
        target:win.terminal
        function onOutput(data){surface.feedLater(data)}
        function onResetScreen(){win.outputThrottled=false;if(win.terminal)win.terminal.setBackpressure(false);searchDelay.stop();surface.reset();searchInput.clear();win.searchOpen=false}
        function onChanged(){if(win.terminal.running && win.visible && !win.searchOpen)surface.forceActiveFocus()}
    }
    Text {
        x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:8
        width:Math.max(20,win.width-x-12);elide:Text.ElideRight
        text:win.searchOpen ? surface.searchStatus : (surface.scrollOffset>0 ? "Historique de sortie · "+surface.scrollOffset+" lignes au-dessus" : (win.terminal?win.terminal.status:"Déconnecté"));color:Theme.inkMuted;font.pixelSize:11
    }
    Dialog {
        id:pasteDialog
        anchors.centerIn:parent;width:Math.min(440,win.width-24);modal:true
        title:"Envoyer ce texte au shell ?";contentHeight:Math.min(220,Math.max(84,win.pasteText.length/2+90))
        standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:Text { objectName:"pastePreview";text:win.pasteText.length>16000?"Texte trop volumineux (limite 16 Ko).":"Le texte suivant sera envoyé au serveur. Les retours à la ligne peuvent exécuter des commandes.\n\n"+win.pasteText.slice(0,300)+(win.pasteText.length>300?"…":"");wrapMode:Text.WrapAnywhere;font.family:Theme.monoFamily;font.pixelSize:12 }
        onAccepted:{if(win.pasteText.length<=16000)surface.paste(win.pasteText);win.pasteText="";surface.forceActiveFocus()}
        onRejected:win.pasteText=""
    }
}
