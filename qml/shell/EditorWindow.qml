import QtQuick
import QtQuick.Controls.Fusion

// Éditeur: plain-text / code files of the connected server (any absolute path, UTF-8, 256 Kio).
// Same safe pipeline as MDok (hash-checked SFTP atomic save, server-side draft, conflict screen)
// through a DocumentsController in code mode. One document at a time; no syntax colouring yet.
Item {
    id:win
    objectName:"editorWindow"
    property bool adaptive:false
    property Item backdrop
    property var documents:null
    property var remote:null
    property bool findOpen:false
    property bool replaceMode:false
    property bool syncing:false
    property bool waitingForSave:false
    property bool escapeHandled:false
    property var matchStarts:[]
    property int matchIndex:-1
    property string pendingAction:""
    property string pendingPath:""
    property string pendingServerId:""
    property int pendingGeneration:-1
    property var pendingContinuation:null
    property int caretLine:1
    property int caretColumn:1
    property string lineNumbers:"1"
    readonly property bool connected:remote!==null && remote.connected
    readonly property bool hasDoc:documents!==null && documents.hasDocument
    signal closeRequested()
    signal requestServers()
    signal requestFiles(string path,string serverId)

    DocumentSearch { id:finder }
    CodeHighlighter { document:editor.textDocument;fileName:win.documents && win.documents.path ? win.documents.path : "" }

    // ---- document <-> editor ----
    function syncDocument(){
        if(!editor || !pathInput)return
        syncing=true;editor.text=documents?documents.text:"";syncing=false
        pathInput.text=documents?(documents.path || (connected?remote.currentDirectory+"/nouveau.txt":"")):""
        updateGutter();updateCaret()
        if(visible)editor.forceActiveFocus()
    }
    function normalizedPath(path){
        if(typeof path!=="string" || !path.startsWith("/") || path.indexOf("\u0000")>=0)return path
        const parts=[]
        for(const part of path.split("/")){
            if(!part || part===".")continue
            if(part===".."){if(parts.length===0)return path;parts.pop()}
            else parts.push(part)
        }
        return "/"+parts.join("/")
    }
    function updateGutter(){
        const n=Math.max(1,editor.lineCount)
        if(n===lineNumbers.split("\n").length && n>1)return
        let s="1"
        for(let i=2;i<=n;++i)s+="\n"+i
        lineNumbers=s
    }
    function updateCaret(){
        const before=editor.text.substring(0,editor.cursorPosition)
        const lastBreak=before.lastIndexOf("\n")
        caretLine=before.split("\n").length
        caretColumn=editor.cursorPosition-lastBreak
    }
    function goToLine(n){
        const lines=editor.text.split("\n")
        const target=Math.max(1,Math.min(lines.length,n))
        let offset=0
        for(let i=0;i<target-1;++i)offset+=lines[i].length+1
        editor.cursorPosition=offset;editor.forceActiveFocus()
    }

    // ---- guarded actions (unsaved text is never dropped silently) ----
    function perform(action,path){
        if(action==="close")closeRequested()
        else if(action==="quit")Qt.quit()
        else if(documents){if(action==="open")documents.open(normalizedPath(path));else if(action==="new")documents.createNew()}
    }
    function request(action,path,continuation){
        if(action==="open")path=normalizedPath(path)
        if(documents && documents.busy)return
        if(action==="open" && documents && documents.isCurrentDocument(path)){editor.forceActiveFocus();return}
        if(documents && documents.dirty){
            pendingAction=action;pendingPath=path||"";pendingContinuation=continuation||null
            pendingServerId=remote?remote.activeServerId:"";pendingGeneration=remote?remote.connectionGeneration:-1
            confirm.openWith("Modifications non enregistrées","Enregistrez vos modifications avant de continuer, ou abandonnez-les explicitement.",documents.canSave?"Enregistrer et continuer":"Abandonner",!documents.canSave,false,"")
            confirm.altText=documents.canSave?"Abandonner":""
            return
        }
        if(continuation)continuation();else perform(action,path||"")
    }
    function clearPending(){
        pendingAction="";pendingPath="";pendingServerId="";pendingGeneration=-1;pendingContinuation=null
        waitingForSave=false;confirm.busy=false
    }
    function pendingStillValid(){return remote && remote.activeServerId===pendingServerId && remote.connectionGeneration===pendingGeneration}
    function continuePending(discard){
        if(!pendingStillValid()){
            confirm.error="Le serveur a changé. Annulez puis recommencez sur le bon serveur."
            waitingForSave=false;confirm.busy=false;return
        }
        const action=pendingAction,path=pendingPath,go=pendingContinuation
        clearPending();confirm.visible=false
        if(discard && documents)documents.discardDraft()
        if(go)go();else perform(action,path)
    }
    function saveAndContinue(){
        if(!documents)return
        if(!pendingStillValid()){confirm.error="Le serveur a changé. Annulez puis recommencez.";return}
        if(!documents.canSave){confirm.error="Source indisponible : texte conservé, enregistrement désactivé.";return}
        waitingForSave=true;confirm.busy=true;confirm.error=""
        saveDocument()
    }
    function saveDocument(){
        if(!documents)return
        if(documents.isNew)documents.saveAs(normalizedPath(pathInput.text.trim()));else documents.save()
    }
    // Unsaved text of this editor blocks quitting / switching server (same contract as MDok).
    function requestAllReady(proceed){
        if(!documents || !documents.dirty){proceed();return}
        request("continue","",function(){Qt.callLater(function(){win.requestAllReady(proceed)})})
    }
    function handleBack(){
        if(confirm.visible){if(!confirm.busy){confirm.visible=false;clearPending()}return true}
        if(findOpen){closeFind();return true}
        request("close","");return true
    }

    // ---- find / replace (the TextEdit is edited in place, so Undo keeps working) ----
    function openFind(withReplace){
        findOpen=true;replaceMode=withReplace
        const sel=editor.selectedText
        if(sel.length>0 && sel.indexOf(" ")<0)findField.text=sel
        refreshMatches();findField.forceActiveFocus();findField.selectAll()
    }
    function closeFind(){findOpen=false;editor.forceActiveFocus()}
    function closeFindFromEscape(){escapeHandled=true;closeFind()}
    function refreshMatches(){
        matchStarts=findOpen && findField.text.length>0?finder.matches(editor.text,findField.text,caseButton.checked,wordButton.checked):[]
        matchIndex=-1
        findCount.text=matchStarts.length>0?matchStarts.length+" résultat(s)":(findField.text.length>0?"aucun résultat":"")
    }
    function goTo(forward){
        refreshMatches()
        if(matchStarts.length===0)return
        const cursor=forward?Math.max(editor.selectionEnd,editor.cursorPosition):Math.min(editor.selectionStart,editor.cursorPosition)
        matchIndex=finder.step(matchStarts,cursor,findField.text.length,forward)
        const at=matchStarts[matchIndex]
        editor.select(at,at+findField.text.length)
    }
    function replaceCurrent(){
        if(findField.text.length===0)return
        const same=caseButton.checked?editor.selectedText===findField.text:editor.selectedText.toLowerCase()===findField.text.toLowerCase()
        if(same){
            const at=editor.selectionStart
            editor.remove(at,at+findField.text.length);editor.insert(at,replaceField.text)
            editor.cursorPosition=at+replaceField.text.length
        }
        goTo(true)
    }
    function replaceEverything(){
        refreshMatches()
        const starts=matchStarts.slice(),n=findField.text.length
        if(starts.length===0)return
        syncing=true
        for(let i=starts.length-1;i>=0;--i){editor.remove(starts[i],starts[i]+n);editor.insert(starts[i],replaceField.text)}
        syncing=false
        if(documents)documents.text=editor.text
        refreshMatches();findCount.text=starts.length+" remplacement(s)"
    }

    onDocumentsChanged:syncDocument()
    Component.onCompleted:syncDocument()

    Glass { anchors.fill:parent;backdrop:win.backdrop;tint:"#B3121C30";blurAmount:0.9;radius:16 }

    // ---- title bar ----
    Item {
        id:titleBar
        width:parent.width;height:48
        WindowDragArea { anchors.fill:parent;windowItem:win;movable:!win.adaptive }
        Row { visible:!win.adaptive;x:14;y:17;spacing:9
            Repeater { model:["#FF5F57","#FEBC2E","#28C840"]
                Rectangle { required property string modelData;width:12;height:12;radius:6;color:modelData
                    MouseArea { anchors.fill:parent;onClicked:if(parent.modelData==="#FF5F57")win.request("close","") }
                }
            }
        }
        Text {
            x:win.adaptive?14:84;anchors.verticalCenter:parent.verticalCenter
            width:Math.max(0,parent.width-actions.width-x-12);elide:Text.ElideMiddle;textFormat:Text.PlainText
            text:"Éditeur"+(win.hasDoc?" — "+(win.documents.path?win.documents.path.split("/").pop():"Nouveau fichier")+(win.documents.dirty?" ●":""):"")
            color:Theme.ink;font.pixelSize:15
        }
        Row {
            id:actions;anchors.right:parent.right;anchors.rightMargin:8;anchors.verticalCenter:parent.verticalCenter;spacing:4
            ToolButtonGlass { objectName:"editorNewButton";glyph:"+";touch:win.adaptive;tip:"Nouveau fichier";enabledState:win.connected && win.documents!==null && !win.documents.busy;onClicked:win.request("new","") }
            ToolButtonGlass { objectName:"editorOpenButton";glyph:"↗";touch:win.adaptive;tip:"Ouvrir le chemin saisi";enabledState:win.connected && win.documents!==null && !win.documents.busy;onClicked:win.request("open",pathInput.text.trim()) }
            ToolButtonGlass { objectName:"editorSaveButton";glyph:"✓";touch:win.adaptive;tip:"Enregistrer (Ctrl+S)";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && win.hasDoc;onClicked:win.saveDocument() }
            ToolButtonGlass { objectName:"editorSaveCopyButton";glyph:"⧉";touch:win.adaptive;tip:"Enregistrer sous le chemin saisi";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && win.hasDoc;onClicked:win.documents.saveAs(win.normalizedPath(pathInput.text.trim())) }
            ToolButtonGlass { glyph:"⌕";touch:win.adaptive;tip:"Rechercher (Ctrl+F)";enabledState:win.hasDoc;onClicked:win.openFind(false) }
            ToolButtonGlass { glyph:"×";touch:win.adaptive;tip:"Fermer";onClicked:win.request("close","") }
        }
    }
    TextField {
        id:pathInput;objectName:"editorPath"
        x:12;y:52;width:win.width-24;height:win.adaptive?44:34
        placeholderText:"Chemin absolu du fichier sur le serveur";selectByMouse:true
        enabled:!win.documents || !win.documents.busy
        onAccepted:win.request("open",text.trim())
    }

    // ---- server draft found for this file ----
    Rectangle {
        id:recoveryBanner;objectName:"editorRecoveryBanner"
        visible:win.documents!==null && win.documents.recovery!==undefined && win.documents.recovery.available===true && !win.documents.dirty
        x:12;y:pathInput.y+pathInput.height+6;width:win.width-24;height:visible?(win.adaptive?84:62):0;radius:10;color:"#33FCD34D";border.color:"#66FCD34D"
        Text {
            x:10;y:6;width:parent.width-20;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:12
            text:win.documents && win.documents.recovery!==undefined && win.documents.recovery.available===true
                 ?"Brouillon non enregistré du "+win.documents.recovery.savedAt+" trouvé sur le serveur ("+win.documents.recovery.changedLines+" ligne(s) différentes)"+(win.documents.recovery.baseMatches?".":" — le fichier a changé depuis.")
                 :""
        }
        Row {
            x:10;anchors.bottom:parent.bottom;anchors.bottomMargin:6;spacing:8
            Button { objectName:"editorRestoreDraft";text:"Restaurer";onClicked:win.documents.restoreDraft() }
            Button { objectName:"editorKeepDraft";text:"Plus tard";onClicked:win.documents.dismissRecovery(false) }
            Button { objectName:"editorDeleteDraft";text:"Supprimer";onClicked:win.documents.dismissRecovery(true) }
        }
    }

    // ---- find bar ----
    Rectangle {
        id:findBar;objectName:"editorFindBar"
        visible:win.findOpen && win.hasDoc
        x:12;y:pathInput.y+pathInput.height+6+(recoveryBanner.visible?recoveryBanner.height+6:0);width:win.width-24
        height:visible?(win.replaceMode?(win.adaptive?96:70):(win.adaptive?48:36)):0;radius:10;color:"#26FFFFFF";border.color:"#40FFFFFF"
        Row {
            x:8;y:4;spacing:6;height:win.adaptive?40:28
            TextField {
                id:findField;objectName:"editorFind";width:Math.max(80,findBar.width-(win.adaptive?340:300));height:parent.height;placeholderText:"Rechercher";selectByMouse:true
                onTextChanged:win.refreshMatches()
                onAccepted:win.goTo(true)
                Keys.onPressed:event=>{
                    if(event.key===Qt.Key_Escape){win.closeFindFromEscape();event.accepted=true}
                    else if((event.key===Qt.Key_Return||event.key===Qt.Key_Enter)&&(event.modifiers&Qt.ShiftModifier)){win.goTo(false);event.accepted=true}
                }
            }
            Button { objectName:"editorFindPrev";width:win.adaptive?44:30;height:parent.height;text:"↑";onClicked:win.goTo(false) }
            Button { objectName:"editorFindNext";width:win.adaptive?44:30;height:parent.height;text:"↓";onClicked:win.goTo(true) }
            Button { id:caseButton;width:win.adaptive?44:34;height:parent.height;text:"Aa";checkable:true;onToggled:win.refreshMatches() }
            Button { id:wordButton;width:win.adaptive?54:40;height:parent.height;text:"Mot";checkable:true;onToggled:win.refreshMatches() }
            Text { id:findCount;objectName:"editorFindCount";height:parent.height;verticalAlignment:Text.AlignVCenter;color:Theme.inkMuted;font.pixelSize:11;text:"" }
        }
        Row {
            visible:win.replaceMode
            x:8;y:(win.adaptive?52:36);spacing:6;height:win.adaptive?40:28
            TextField {
                id:replaceField;objectName:"editorReplace";width:Math.max(80,findBar.width-(win.adaptive?340:300));height:parent.height;placeholderText:"Remplacer par";selectByMouse:true
                Keys.onPressed:event=>{if(event.key===Qt.Key_Escape){win.closeFindFromEscape();event.accepted=true}}
            }
            Button { height:parent.height;text:"Remplacer";onClicked:win.replaceCurrent() }
            Button { objectName:"editorReplaceAll";height:parent.height;text:"Tout";onClicked:win.replaceEverything() }
        }
        Button { x:parent.width-(win.adaptive?48:34);y:4;width:win.adaptive?44:30;height:win.adaptive?40:28;text:"×";onClicked:win.closeFind() }
    }

    // ---- text area with line numbers ----
    Item {
        id:body
        x:12;y:findBar.y+(findBar.visible?findBar.height+6:0);width:win.width-24;height:Math.max(32,win.height-y-34)
        visible:win.hasDoc
        Rectangle { anchors.fill:parent;radius:8;color:"#33050B18";border.color:"#22FFFFFF" }
        Flickable {
            id:scroll;objectName:"editorScroll"
            anchors.fill:parent;anchors.margins:1;clip:true
            contentWidth:Math.max(width,gutter.width+editor.contentWidth+40);contentHeight:Math.max(height,editor.contentHeight+16)
            boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            ScrollBar.horizontal:ScrollBar { policy:ScrollBar.AsNeeded }
            function ensureCursorVisible(){
                const r=editor.cursorRectangle
                const top=editor.y+r.y,bottom=top+r.height,left=editor.x+r.x,right=left+r.width
                if(top<contentY+8)contentY=Math.max(0,top-8)
                else if(bottom>contentY+height-8)contentY=Math.min(contentHeight-height,bottom-height+8)
                if(left<contentX+gutter.width+8)contentX=Math.max(0,left-gutter.width-8)
                else if(right>contentX+width-12)contentX=Math.min(contentWidth-width,right-width+12)
            }
            Text {
                id:gutter;objectName:"editorGutter"
                x:0;y:8;width:Math.max(34,implicitWidth+14);horizontalAlignment:Text.AlignRight;rightPadding:8
                text:win.lineNumbers;textFormat:Text.PlainText;color:Theme.inkMuted
                font.family:"monospace";font.pixelSize:win.adaptive?15:13
            }
            TextEdit {
                id:editor;objectName:"editorText"
                x:gutter.width+4;y:8;width:Math.max(parent.width-x-12,implicitWidth)
                textFormat:TextEdit.PlainText;wrapMode:TextEdit.NoWrap
                color:Theme.ink;selectionColor:Theme.accent;font.family:"monospace";font.pixelSize:win.adaptive?15:13
                selectByMouse:true;persistentSelection:true;tabStopDistance:32
                readOnly:win.documents!==null && win.documents.busy
                onTextChanged:{if(!win.syncing && win.documents)win.documents.text=text;win.updateGutter();win.updateCaret()}
                onCursorPositionChanged:{win.updateCaret();scroll.ensureCursorVisible()}
            }
        }
    }

    // ---- placeholder when no document ----
    Column {
        visible:!win.hasDoc
        x:16;y:170;width:win.width-32;spacing:14
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"Éditeur · fichiers texte et code du serveur";color:Theme.ink;font.pixelSize:16 }
        Button { objectName:"editorConnectButton";visible:!win.connected;anchors.horizontalCenter:parent.horizontalCenter;text:"Choisir un serveur…";onClicked:win.requestServers() }
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"Saisissez le chemin absolu d’un fichier UTF-8 (256 Kio maximum), ou ouvrez-le depuis Fichiers.";color:Theme.inkMuted;font.pixelSize:12 }
    }

    // ---- status line ----
    Text {
        x:14;anchors.bottom:parent.bottom;anchors.bottomMargin:10;width:parent.width-210;elide:Text.ElideRight;textFormat:Text.PlainText
        color:Theme.inkMuted;font.pixelSize:11
        text:win.documents?win.documents.status+(win.documents.draftStatus?" · "+win.documents.draftStatus:""):""
    }
    Text {
        objectName:"editorCaret";visible:win.hasDoc
        anchors.right:parent.right;anchors.rightMargin:14;anchors.bottom:parent.bottom;anchors.bottomMargin:10
        color:Theme.inkMuted;font.pixelSize:11
        text:"Ln "+win.caretLine+", Col "+win.caretColumn+" · "+Math.max(1,editor.lineCount)+" lignes"
    }

    // ---- conflict: the file changed on the server under a save ----
    Rectangle {
        id:conflictPanel;objectName:"editorConflict";z:50
        visible:win.documents!==null && win.documents.conflictActive
        x:12;y:pathInput.y+pathInput.height+8;width:win.width-24;height:Math.max(120,win.height-y-12);radius:12;color:"#F2121C30";border.color:"#40FFFFFF"
        Text { x:14;y:10;width:parent.width-28;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:14;text:"Conflit : le fichier a changé sur le serveur" }
        Text { x:14;y:36;width:parent.width-28;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:12;text:win.documents?win.documents.conflictSummary:"" }
        Flickable {
            x:14;y:72;width:parent.width-28;height:parent.height-72-(win.adaptive?104:56);clip:true;contentWidth:width;contentHeight:diffText.implicitHeight+8;boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            Text { id:diffText;objectName:"editorConflictDiff";width:parent.width;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:Theme.ink;font.family:"monospace";font.pixelSize:12;text:win.documents?win.documents.conflictDiff:"" }
        }
        Flow {
            x:14;anchors.bottom:parent.bottom;anchors.bottomMargin:10;width:parent.width-28;spacing:8
            Button { objectName:"editorConflictCopy";enabled:win.documents!==null && !win.documents.conflictBusy && !win.documents.busy;text:"Enregistrer ma version comme copie";onClicked:win.documents.resolveConflictSaveCopy() }
            Button { objectName:"editorConflictServer";enabled:win.documents!==null && !win.documents.conflictBusy && !win.documents.busy;text:"Charger la version du serveur";onClicked:win.documents.resolveConflictTakeServer() }
            Button { objectName:"editorConflictClose";text:"Garder mon texte, décider plus tard";onClicked:win.documents.closeConflict() }
        }
    }

    Keys.onPressed:event=>{
        const ctrl=event.modifiers&Qt.ControlModifier
        if(ctrl && event.key===Qt.Key_S && win.hasDoc){win.saveDocument();event.accepted=true}
        else if(!win.hasDoc)return
        else if(ctrl && event.key===Qt.Key_F){win.openFind(false);event.accepted=true}
        else if(ctrl && event.key===Qt.Key_H){win.openFind(true);event.accepted=true}
        else if(event.key===Qt.Key_F3){win.findOpen=true;win.goTo(!(event.modifiers&Qt.ShiftModifier));event.accepted=true}
        else if(event.key===Qt.Key_Escape && win.findOpen){win.closeFindFromEscape();event.accepted=true}
    }
    // A handled search Escape must not become a second Back action in the shell.
    Keys.onReleased:event=>{
        if(event.key===Qt.Key_Escape && win.escapeHandled){win.escapeHandled=false;event.accepted=true}
    }
    GlassDialog {
        id:confirm;objectName:"editorUnsavedDialog";anchors.fill:parent;touchMode:win.adaptive
        onAccepted:if(danger)win.continuePending(true);else win.saveAndContinue()
        onAlternate:win.continuePending(true)
        onRejected:win.clearPending()
        Keys.onEscapePressed:event=>{event.accepted=true}
        Keys.onReleased:event=>{if(event.key===Qt.Key_Escape){event.accepted=true;if(!busy){visible=false;win.clearPending();editor.forceActiveFocus()}}}
    }
    Connections { target:win.documents
        function onChanged(){
            if(win.waitingForSave && !win.documents.busy && win.documents.dirty){
                win.waitingForSave=false;confirm.busy=false;confirm.error=win.documents.status
            }
            if(win.documents.conflictActive && confirm.visible){confirm.visible=false;win.clearPending()}
        }
        function onLoaded(){win.syncDocument()}
        function onSaved(path){pathInput.text=path;if(win.waitingForSave)win.continuePending(false)}
    }
}
