import QtQuick
import QtQuick.Controls.Fusion

Item {
    id:win
    objectName:"documentsWindow"
    property bool adaptive:false
    property Item backdrop
    property var documents:null
    property var remote:null
    property bool navigationOpen:false
    property bool previewMode:false
    property bool syncing:false
    property string pendingAction:""
    property string pendingPath:""
    readonly property bool drawer:adaptive && width<900
    readonly property real contentLeft:drawer?0:196
    readonly property bool connected:remote!==null && remote.connected
    readonly property bool split:!adaptive && width-contentLeft>=680
    signal closeRequested()
    signal requestServers()
    function syncDocument(){
        if(!editor || !pathInput)return
        syncing=true;editor.text=documents?documents.text:"";syncing=false
        pathInput.text=documents?(documents.path || (connected?remote.currentDirectory+"/Nouveau document.md":"")):""
        previewTimer.restart()
        if(visible && (split || !previewMode))editor.forceActiveFocus()
    }
    function perform(action,path){
        if(action==="close")closeRequested()
        else if(action==="quit")Qt.quit()
        else if(documents){if(action==="open")documents.open(path);else if(action==="new")documents.createNew()}
    }
    function request(action,path){
        if(documents && documents.busy)return
        if(documents && documents.dirty){pendingAction=action;pendingPath=path||"";confirm.openWith("Modifications non enregistrées","Conservez le brouillon en enregistrant ou en copiant son texte avant de continuer.","Abandonner le brouillon",true,false,"");return}
        perform(action,path||"")
    }
    function allowAppClose(){if(documents && documents.busy)return false;if(documents && documents.dirty){request("quit","");return false}return true}
    function handleBack(){
        if(confirm.visible){confirm.visible=false;return true}
        if(navigationOpen){navigationOpen=false;return true}
        if(!split && previewMode){previewMode=false;return true}
        request("close","");return true
    }
    onDocumentsChanged:syncDocument()
    Component.onCompleted:syncDocument()
    Glass { anchors.fill:parent;backdrop:win.backdrop;tint:"#B3121C30";blurAmount:0.9;radius:16 }
    MouseArea { visible:win.drawer && win.navigationOpen;anchors.fill:parent;z:30;onClicked:win.navigationOpen=false }
    Rectangle {
        objectName:"documentsSidebar"
        visible:!win.drawer || win.navigationOpen;width:win.drawer?Math.min(260,win.width-32):196;height:parent.height;z:win.drawer?40:0
        topLeftRadius:16;bottomLeftRadius:16;color:win.drawer?"#F0121C30":"#40050B18"
        WindowDragArea { anchors.fill:parent;windowItem:win;movable:!win.adaptive }
        Row { visible:!win.adaptive;x:14;y:17;spacing:9
            Repeater { model:["#FF5F57","#FEBC2E","#28C840"]
                Rectangle { required property string modelData;width:12;height:12;radius:6;color:modelData
                    MouseArea { anchors.fill:parent;onClicked:if(parent.modelData==="#FF5F57")win.request("close","") }
                }
            }
        }
        Column {
            x:16;y:54;width:parent.width-32;spacing:16
            Text { text:"DOCUMENT MARKDOWN";color:Theme.inkMuted;font.pixelSize:10 }
            Text { width:parent.width;textFormat:Text.PlainText;wrapMode:Text.WrapAnywhere;text:win.documents && win.documents.hasDocument?(win.documents.path||"Nouveau document"):"Aucun document ouvert";color:Theme.ink;font.pixelSize:12 }
            Text { width:parent.width;textFormat:Text.PlainText;wrapMode:Text.WrapAnywhere;text:win.documents?win.documents.sourceLabel:"";color:Theme.inkMuted;font.pixelSize:11 }
            Text { width:parent.width;wrapMode:Text.WordWrap;text:"Markdown UTF-8\n256 Kio maximum\n\nLes images et liens externes ne sont pas chargés.\n\nPDF : pas encore disponible.";color:Theme.inkMuted;font.pixelSize:11 }
        }
    }
    Item {
        x:win.contentLeft;width:parent.width-x;height:48
        WindowDragArea { anchors.fill:parent;windowItem:win;movable:!win.adaptive }
        ToolButtonGlass { visible:win.drawer;x:4;y:2;touch:true;glyph:"☰";onClicked:win.navigationOpen=!win.navigationOpen }
        Text { x:win.drawer?54:14;anchors.verticalCenter:parent.verticalCenter;width:Math.max(0,parent.width-actions.width-x-12);elide:Text.ElideRight;text:"MDok"+(win.documents && win.documents.dirty?" ●":"");color:Theme.ink;font.pixelSize:14 }
        Row {
            id:actions;anchors.right:parent.right;anchors.rightMargin:8;anchors.verticalCenter:parent.verticalCenter;spacing:4
            ToolButtonGlass { glyph:"+";touch:win.adaptive;tip:"Nouveau Markdown";enabledState:win.connected && win.documents!==null && !win.documents.busy;onClicked:win.request("new","") }
            ToolButtonGlass { objectName:"documentsOpenButton";glyph:"↗";touch:win.adaptive;tip:"Ouvrir le chemin saisi";enabledState:win.connected && win.documents!==null && !win.documents.busy;onClicked:win.request("open",pathInput.text) }
            ToolButtonGlass { objectName:"documentsSaveButton";glyph:"✓";touch:win.adaptive;tip:"Enregistrer (Ctrl+S)";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && (win.documents.dirty||win.documents.isNew);onClicked:win.saveDocument() }
            ToolButtonGlass { glyph:"⧉";touch:win.adaptive;tip:"Enregistrer une copie au chemin saisi";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy;onClicked:win.documents.saveAs(pathInput.text) }
            ToolButtonGlass { glyph:"eye";touch:win.adaptive;tip:"Édition / aperçu";checked:win.previewMode;onClicked:win.previewMode=!win.previewMode }
            ToolButtonGlass { glyph:"×";touch:win.adaptive;tip:"Fermer";onClicked:win.request("close","") }
        }
    }
    function saveDocument(){if(documents){if(documents.isNew)documents.saveAs(pathInput.text);else documents.save()}}
    TextField {
        id:pathInput;objectName:"documentsPath"
        x:win.contentLeft+12;y:52;width:win.width-x-12;height:win.adaptive?44:34
        placeholderText:"Chemin absolu du document .md sur le serveur";selectByMouse:true
        enabled:!win.documents || !win.documents.busy
        onAccepted:win.request("open",text)
    }
    Item {
        id:body;x:win.contentLeft+12;y:pathInput.y+pathInput.height+8;width:win.width-x-12;height:Math.max(32,win.height-y-80)
        visible:win.documents!==null && win.documents.hasDocument
        Flickable {
            id:editScroll;objectName:"documentsEditorScroll";visible:win.split || !win.previewMode
            width:win.split?(parent.width-8)/2:parent.width;height:parent.height
            clip:true;contentWidth:width;contentHeight:editor.contentHeight+24;boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            TextEdit {
                id:editor;objectName:"documentsEditor"
                x:10;y:8;width:parent.width-24;textFormat:TextEdit.PlainText;wrapMode:TextEdit.Wrap
                color:Theme.ink;selectionColor:Theme.accent;font.pixelSize:14;selectByMouse:true
                readOnly:win.documents!==null && win.documents.busy
                onTextChanged:{if(!win.syncing && win.documents)win.documents.text=text;previewTimer.restart()}
                onCursorRectangleChanged:{if(cursorRectangle.y<editScroll.contentY)editScroll.contentY=Math.max(0,cursorRectangle.y);else if(cursorRectangle.y+cursorRectangle.height>editScroll.contentY+editScroll.height)editScroll.contentY=cursorRectangle.y+cursorRectangle.height-editScroll.height+16}
            }
        }
        Flickable {
            id:previewScroll;objectName:"documentsPreviewScroll";visible:win.split || win.previewMode
            x:win.split?(parent.width+8)/2:0;width:win.split?(parent.width-8)/2:parent.width;height:parent.height
            clip:true;contentWidth:width;contentHeight:preview.documentHeight;boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            MarkdownPreview { id:preview;objectName:"documentsPreview";y:previewScroll.contentY;width:previewScroll.width;height:previewScroll.height;scrollY:previewScroll.contentY }
        }
    }
    Timer { id:previewTimer;interval:180;onTriggered:{preview.source=editor.text} }
    Column {
        visible:!win.documents || !win.documents.hasDocument
        x:win.contentLeft+16;y:160;width:win.width-x-16;spacing:14
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"MDok · documents Markdown du serveur";color:Theme.ink;font.pixelSize:16 }
        Button { objectName:"documentsConnectButton";visible:!win.connected;anchors.horizontalCenter:parent.horizontalCenter;text:"Choisir un serveur…";onClicked:win.requestServers() }
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"Ouvrez un .md depuis Fichiers ou saisissez son chemin ci-dessus.";color:Theme.inkMuted;font.pixelSize:12 }
    }
    Text { x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:34;width:win.width-x-12;height:42;wrapMode:Text.WordWrap;maximumLineCount:3;elide:Text.ElideRight;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:11;text:win.documents?win.documents.status:"Choisissez un serveur Linux." }
    ToolButtonGlass { x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:4;glyph:"⧉";tip:"Copier le brouillon dans le presse-papiers";enabledState:win.documents!==null && win.documents.hasDocument;onClicked:{const start=editor.selectionStart,end=editor.selectionEnd;editor.selectAll();editor.copy();editor.select(start,end)} }
    Text { x:win.contentLeft+50;anchors.bottom:parent.bottom;anchors.bottomMargin:10;visible:preview.limited;text:"Aperçu limité aux 64 000 premiers caractères";color:Theme.inkMuted;font.pixelSize:10 }
    Keys.onPressed:event=>{if(event.key===Qt.Key_S && event.modifiers&Qt.ControlModifier){win.saveDocument();event.accepted=true}}
    GlassDialog {
        id:confirm;objectName:"documentsUnsavedDialog";anchors.fill:parent;touchMode:win.adaptive
        onAccepted:{visible=false;if(win.documents)win.documents.discardDraft();win.perform(win.pendingAction,win.pendingPath)}
        onRejected:win.pendingAction=""
        Keys.onEscapePressed:event=>{event.accepted=true}
        Keys.onReleased:event=>{if(event.key===Qt.Key_Escape){event.accepted=true;visible=false;win.pendingAction="";editor.forceActiveFocus()}}
    }
    Connections { target:win.documents
        function onLoaded(){win.syncDocument()}
        function onSaved(path){pathInput.text=path}
    }
}
