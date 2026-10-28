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
    property string pendingServerId:""
    property int pendingGeneration:-1
    property var pendingContinuation:null
    property bool waitingForSave:false
    property bool findOpen:false
    property bool escapeHandled:false
    property bool replaceMode:false
    property var matchStarts:[]
    property int matchIndex:-1
    property var stats:({ "words":0, "characters":0, "lines":0 })
    property var headings:[]
    property bool outlineOpen:true
    property bool imageInsertOpen:false
    property var recentDocuments:[]
    property string recentServerId:""
    property var pinnedTabs:[]
    property var tabRoot:null
    property var tabSessions:({})
    property int tabGeneration:-1
    DocumentSearch { id:finder }
    MarkdownHighlighter { document:editor.textDocument }
    readonly property bool drawer:adaptive && width<900
    readonly property real contentLeft:drawer?0:196
    readonly property bool connected:remote!==null && remote.connected
    readonly property bool split:!adaptive && width-contentLeft>=680
    signal closeRequested()
    signal requestServers()
    signal requestFiles(string path, string serverId)
    property var loadedImages:({})
    function refreshImageResources(){
        if(!documents || !documents.hasDocument || documents.pdfMode || documents.isNew)return
        const regex=/!\[[^\]]*\]\(([^\s)]+)(?:\s+["'][^"']*["'])?\)/g
        const source=editor.text.slice(0,64000)
        let match, count=0
        while((match=regex.exec(source))!==null && count<12){
            const path=match[1]
            if(!loadedImages[path]){loadedImages[path]=true;documents.requestMarkdownImage(path);count++}
        }
    }
    function checkTabSession(){
        if(!remote || !remote.connected)return
        if(tabGeneration!==remote.connectionGeneration || recentServerId!==remote.activeServerId){
            tabSessions=({});pinnedTabs=[];recentDocuments=[]
            tabGeneration=remote.connectionGeneration;recentServerId=remote.activeServerId
        }
    }
    function trackDocument(){
        if(!tabRoot && documents)tabRoot=documents
        if(!documents || !documents.hasDocument || documents.isNew || !documents.path || !remote)return
        if(!documents.sourceAvailable)return
        checkTabSession()
        const server=documents.sourceServerId
        if(!server)return
        if(recentServerId!==server){recentDocuments=[];pinnedTabs=[];recentServerId=server}
        const path=documents.path
        const entries=recentDocuments.filter(p=>p!==path)
        entries.unshift(path)
        recentDocuments=entries.slice(0,10)
        if(pinnedTabs.indexOf(path)<0)pinnedTabs=pinnedTabs.concat([path]).slice(-8)
        if(!tabSessions[server+"|"+path])tabSessions[server+"|"+path]=documents
    }
    function requestAllDocumentsReady(proceed){
        if(!tabRoot){proceed();return}
        if(tabRoot.hasBusyTabs())return
        const dirty=tabRoot.dirtyTabSessions()
        if(dirty.length===0){proceed();return}
        const tab=dirty[0]
        if(documents!==tab){documents=tab;syncDocument()}
        const next=function(){Qt.callLater(function(){win.requestAllDocumentsReady(proceed)})}
        request("continue","",next)
    }
    function closePinnedTab(path){
        const key=recentServerId+"|"+path
        const tab=tabSessions[key]
        if(!tab)return
        if(tab.busy){documents=tab;syncDocument();return}
        if(tab.dirty){
            documents=tab;syncDocument()
            request("close","",function(){win.closePinnedTab(path)})
            return
        }
        const remaining=pinnedTabs.filter(p=>p!==path)
        pinnedTabs=remaining
        delete tabSessions[key]
        if(documents===tab){
            if(remaining.length>0){
                const next=tabSessions[recentServerId+"|"+remaining[remaining.length-1]]
                if(next){documents=next;syncDocument()}
                else {documents=tabRoot;syncDocument()}
            }else if(tab!==tabRoot){documents=tabRoot;syncDocument()}
            else win.closeRequested()
        }
        if(tab!==tabRoot && documents!==tab && tabRoot)tabRoot.releaseTabSession(tab)
    }
    function rekeySavedTab(path){
        if(!documents || !path || !remote)return
        const prefix=remote.activeServerId+"|"
        const keys=Object.keys(tabSessions)
        for(let i=0;i<keys.length;i++){
            if(tabSessions[keys[i]]===documents && keys[i]!==prefix+path){
                const oldPath=keys[i].substring(prefix.length)
                if(keys[i].startsWith(prefix)){
                    delete tabSessions[keys[i]]
                    pinnedTabs=pinnedTabs.filter(p=>p!==oldPath)
                }
            }
        }
        tabSessions[prefix+path]=documents
        trackDocument()
    }
    function openRecent(path){
        path=normalizedDocumentPath(path)
        if(!remote || !remote.connected || remote.activeServerId!==recentServerId)return
        checkTabSession()
        const key=recentServerId+"|"+path
        const tab=tabSessions[key]
        if(tab){
            if(documents && documents.busy)return
            documents=tab
            syncDocument()
        }else request("open",path)
    }
    function syncDocument(){
        if(!editor || !pathInput)return
        syncing=true;editor.text=documents?documents.text:"";syncing=false
        loadedImages=({});preview.clearImages()
        pathInput.text=documents?(documents.path || (connected?remote.currentDirectory+"/Nouveau document.md":"")):""
        previewTimer.restart()
        trackDocument()
        if(visible && (split || !previewMode))editor.forceActiveFocus()
    }
    function normalizedDocumentPath(path){
        if(typeof path!=="string" || !path.startsWith("/") || path.indexOf("\u0000")>=0)return path
        const parts=[]
        for(const part of path.split("/")){
            if(!part || part===".")continue
            if(part===".."){if(parts.length===0)return path;parts.pop()}
            else parts.push(part)
        }
        return "/"+parts.join("/")
    }
    function perform(action,path){
        if(action==="open")path=normalizedDocumentPath(path)
        if(action==="open" && path && tabRoot && remote && remote.connected){
            checkTabSession()
            const key=remote.activeServerId+"|"+path
            if(tabSessions[key]){documents=tabSessions[key];syncDocument();return}
            if(documents && documents.hasDocument && documents.path!==path){
                if(documents.path && !documents.isNew && documents.sourceAvailable)tabSessions[remote.activeServerId+"|"+documents.path]=documents
                const newSession=tabRoot.createTabSession()
                tabSessions[key]=newSession
                documents=newSession
            }
        }
        if(action==="close")closeRequested()
        else if(action==="quit")Qt.quit()
        else if(documents){if(action==="open"){if(path.toLowerCase().endsWith(".pdf"))documents.openPdf(path);else documents.open(path)}else if(action==="new")documents.createNew()}
    }
    function clearPending(){
        pendingAction="";pendingPath="";pendingServerId="";pendingGeneration=-1;pendingContinuation=null
        waitingForSave=false;confirm.busy=false
    }
    function pendingStillValid(){
        return remote && remote.activeServerId===pendingServerId && remote.connectionGeneration===pendingGeneration
    }
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
    function request(action,path,continuation){
        if(action==="open")path=normalizedDocumentPath(path)
        if(documents && documents.busy)return
        if(action==="open" && documents && documents.isCurrentDocument(path)){
            editor.forceActiveFocus();return
        }
        // Untitled drafts have no stable tab identity: never switch away from them silently.
        const isTabSwitch=action==="open" && tabRoot && remote && remote.connected && path && documents && !documents.isNew && documents.sourceAvailable
        if(documents && documents.dirty && !isTabSwitch){
            pendingAction=action;pendingPath=path||"";pendingContinuation=continuation||null
            pendingServerId=remote?remote.activeServerId:"";pendingGeneration=remote?remote.connectionGeneration:-1
            confirm.openWith("Modifications non enregistrées","Enregistrez vos modifications avant de continuer, ou abandonnez-les explicitement.",documents.canSave?"Enregistrer et continuer":"Abandonner",!documents.canSave,false,"")
            confirm.altText=documents.canSave?"Abandonner":""
            return
        }
        if(continuation)continuation();else perform(action,path||"")
    }
    // ---- find / replace (the TextEdit is edited in place, so Undo keeps working) ----
    function openFind(withReplace){
        findOpen=true;replaceMode=withReplace
        const sel=editor.selectedText
        if(sel.length>0 && sel.indexOf("\n")<0)findField.text=sel
        refreshMatches();findField.forceActiveFocus();findField.selectAll()
    }
    function closeFind(){findOpen=false;editor.forceActiveFocus()}
    function closeFindFromEscape(){escapeHandled=true;closeFind()}
    function refreshMatches(){
        matchStarts=findOpen && findField.text.length>0?finder.matches(editor.text,findField.text,caseButton.checked,wordButton.checked):[]
        matchIndex=-1
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
        previewTimer.restart();refreshMatches()
        findCount.text=starts.length+" remplacement(s)"
    }
    function wrapSelection(prefix,suffix,selectInner){
        const a=editor.selectionStart,b=editor.selectionEnd
        if(a===b){editor.insert(a,prefix+suffix);editor.cursorPosition=a+prefix.length;return}
        editor.insert(b,suffix);editor.insert(a,prefix)
        if(selectInner)editor.select(b+prefix.length+suffix.indexOf("(")+1,b+prefix.length+suffix.length-1)
        else editor.select(a+prefix.length,b+prefix.length)
    }
    function updateOutline(){
        const lines=editor.text.split("\n"), result=[]
        let offset=0, fenced=false, fenceChar="", fenceLength=0
        for(let i=0;i<lines.length;i++){
            const line=lines[i]
            const fence=/^ {0,3}(`{3,}|~{3,})/.exec(line)
            if(fence){
                const mark=fence[1]
                if(!fenced){fenced=true;fenceChar=mark[0];fenceLength=mark.length}
                else if(mark[0]===fenceChar && mark.length>=fenceLength && line.slice(fence[0].length).trim()==="")fenced=false
            }else if(!fenced){
                const match=/^ {0,3}(#{1,6})[ \t]+(.+?)\s*#*\s*$/.exec(line)
                if(match && result.length<200)result.push({level:match[1].length,title:match[2],offset:offset,line:i+1})
            }
            offset+=line.length+1
        }
        headings=result
    }
    function jumpToHeading(offset){
        if(win.previewMode && !win.split)win.previewMode=false
        editor.cursorPosition=Math.min(offset,editor.length)
        editor.forceActiveFocus()
        win.navigationOpen=false
    }
    function insertBlock(prefix){
        const a=editor.selectionStart,b=editor.selectionEnd
        if(a===b){editor.insert(a,prefix);editor.cursorPosition=a+prefix.length}
        else {
            const selected=editor.selectedText.replace(/\u2029/g,"\n")
            editor.remove(a,b);editor.insert(a,selected.split("\n").map(line=>prefix+line).join("\n"))
        }
        editor.forceActiveFocus()
    }
    function handleBack(){
        if(confirm.visible){if(!confirm.busy){confirm.visible=false;clearPending()}return true}
        if(navigationOpen){navigationOpen=false;return true}
        if(findOpen){closeFind();return true}
        if(!split && previewMode){previewMode=false;return true}
        request("close","");return true
    }
    onDocumentsChanged:syncDocument()
    Component.onCompleted:{tabRoot=documents;syncDocument()}
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
            Text { text:win.documents && win.documents.pdfMode?"DOCUMENT PDF":"DOCUMENT MARKDOWN";color:Theme.inkMuted;font.pixelSize:10 }
            Text { width:parent.width;textFormat:Text.PlainText;wrapMode:Text.WrapAnywhere;text:win.documents && win.documents.hasDocument?(win.documents.path||"Nouveau document"):"Aucun document ouvert";color:Theme.ink;font.pixelSize:12 }
            Text { width:parent.width;textFormat:Text.PlainText;wrapMode:Text.WrapAnywhere;text:win.documents?win.documents.sourceLabel:"";color:Theme.inkMuted;font.pixelSize:11 }
            Text { text:"DOCUMENTS RÉCENTS";color:Theme.inkMuted;font.pixelSize:10 }
            ListView {
                objectName:"documentsRecentFiles"
                width:parent.width
                height:Math.min(170,win.recentDocuments.length*34)
                clip:true
                visible:win.recentDocuments.length>0 && win.connected && win.remote.activeServerId===win.recentServerId
                model:win.recentDocuments
                delegate:Button {
                    required property string modelData
                    width:ListView.view.width;height:32
                    text:modelData.split("/").pop()
                    font.pixelSize:11
                    highlighted:win.documents && win.documents.path===modelData
                    ToolTip.visible:hovered
                    ToolTip.text:modelData
                    onClicked:win.openRecent(modelData)
                }
            }
            Button {
                objectName:"documentsShowInFiles";width:parent.width;text:"Afficher dans Fichiers"
                enabled:win.documents!==null && win.documents.hasDocument && !win.documents.isNew
                    && win.connected && win.documents.sourceAvailable && win.documents.sourceServerId===win.remote.activeServerId
                onClicked:{win.navigationOpen=false;win.requestFiles(win.documents.path,win.documents.sourceServerId)}
            }
            Button { width:parent.width;text:win.outlineOpen?"Plan du document ▾":"Plan du document ▸";onClicked:win.outlineOpen=!win.outlineOpen }
            Text { visible:win.outlineOpen && win.headings.length===0;width:parent.width;text:"Aucun titre Markdown";color:Theme.inkMuted;font.pixelSize:11 }
            ListView {
                objectName:"documentsOutline"
                visible:win.outlineOpen && win.headings.length>0
                width:parent.width;height:visible?Math.min(270,Math.max(64,win.height-375)):0
                clip:true;model:win.headings
                delegate:Button {
                    required property var modelData
                    width:ListView.view.width
                    height:30
                    leftPadding:Math.min(36,(modelData.level-1)*9)+4
                    text:modelData.title
                    font.pixelSize:11
                    onClicked:win.jumpToHeading(modelData.offset)
                    ToolTip.visible:hovered;ToolTip.text:"Ligne "+modelData.line
                }
            }
            Text { width:parent.width;wrapMode:Text.WordWrap;text:"Markdown UTF-8 (256 Kio maximum) et PDF en lecture seule via Poppler sur le serveur. Aucune ressource externe chargée.";color:Theme.inkMuted;font.pixelSize:11 }
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
            ToolButtonGlass { objectName:"documentsSaveButton";glyph:"✓";touch:win.adaptive;tip:"Enregistrer (Ctrl+S)";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && !win.documents.pdfMode && (win.documents.dirty||win.documents.isNew);onClicked:win.saveDocument() }
            ToolButtonGlass { glyph:"⧉";touch:win.adaptive;tip:"Enregistrer une copie au chemin saisi";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && !win.documents.pdfMode;onClicked:win.documents.saveAs(pathInput.text) }
            ToolButtonGlass { glyph:"PDF";touch:win.adaptive;tip:"Exporter Markdown en PDF sur le serveur";enabledState:win.documents!==null && win.documents.canSave && !win.documents.busy && !win.documents.pdfMode;onClicked:{const path=win.documents.path || pathInput.text;const output=path.replace(/\.(md|markdown)$/i,".pdf");if(output.toLowerCase().endsWith(".pdf"))win.documents.exportPdf(output);else win.documents.exportPdf(output+".pdf")} }
            ToolButtonGlass { glyph:"eye";touch:win.adaptive;tip:"Édition / aperçu";checked:win.previewMode;onClicked:win.previewMode=!win.previewMode }
            ToolButtonGlass { glyph:"×";touch:win.adaptive;tip:"Fermer";onClicked:win.request("close","") }
        }
    }
    function saveDocument(){if(documents){if(documents.isNew)documents.saveAs(pathInput.text);else documents.save()}}
    TextField {
        id:pathInput;objectName:"documentsPath"
        x:win.contentLeft+12;y:52;width:win.width-x-12;height:win.adaptive?44:34
        placeholderText:"Chemin absolu du document .md ou .pdf sur le serveur";selectByMouse:true
        enabled:!win.documents || !win.documents.busy
        onAccepted:win.request("open",text)
    }
    Rectangle {
        id:recoveryBanner;objectName:"documentsRecoveryBanner"
        visible:win.documents!==null && win.documents.recovery!==undefined && win.documents.recovery.available===true && !win.documents.dirty
        x:win.contentLeft+12;y:pathInput.y+pathInput.height+6;width:win.width-x-12;height:visible?(win.adaptive?84:62):0;radius:10;color:"#33FCD34D";border.color:"#66FCD34D"
        Text {
            x:10;y:6;width:parent.width-20;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:12
            text:win.documents && win.documents.recovery!==undefined && win.documents.recovery.available===true
                 ?"Brouillon non enregistré du "+win.documents.recovery.savedAt+" trouvé sur le serveur ("+win.documents.recovery.changedLines+" ligne(s) différentes)"+(win.documents.recovery.baseMatches?".":" — le fichier a changé depuis.")
                 :""
        }
        Row {
            x:10;anchors.bottom:parent.bottom;anchors.bottomMargin:6;spacing:8
            Button { objectName:"documentsRestoreDraft";text:"Restaurer";onClicked:win.documents.restoreDraft() }
            Button { objectName:"documentsKeepDraft";text:"Plus tard";onClicked:win.documents.dismissRecovery(false) }
            Button { objectName:"documentsDeleteDraft";text:"Supprimer";onClicked:win.documents.dismissRecovery(true) }
        }
    }
    Rectangle {
        id:findBar;objectName:"documentsFindBar"
        visible:win.findOpen && win.documents!==null && win.documents.hasDocument
        x:win.contentLeft+12;y:pathInput.y+pathInput.height+6+(recoveryBanner.visible?recoveryBanner.height+6:0);width:win.width-x-12
        height:visible?(win.replaceMode?(win.adaptive?96:70):(win.adaptive?48:36)):0;radius:10;color:"#26FFFFFF";border.color:"#40FFFFFF"
        Row {
            x:8;y:4;spacing:6;height:win.adaptive?40:28
            TextField {
                id:findField;objectName:"documentsFind";width:Math.max(80,findBar.width-(win.adaptive?340:300));height:parent.height;placeholderText:"Rechercher";selectByMouse:true
                onTextChanged:{win.refreshMatches();findCount.text=win.matchStarts.length>0?win.matchStarts.length+" résultat(s)":(text.length>0?"aucun résultat":"")}
                onAccepted:win.goTo(true)
                Keys.onPressed:event=>{
                    if(event.key===Qt.Key_Escape){win.closeFindFromEscape();event.accepted=true}
                    else if((event.key===Qt.Key_Return||event.key===Qt.Key_Enter)&&(event.modifiers&Qt.ShiftModifier)){win.goTo(false);event.accepted=true}
                }
            }
            Button { objectName:"documentsFindPrev";width:win.adaptive?44:30;height:parent.height;text:"↑";onClicked:win.goTo(false) }
            Button { objectName:"documentsFindNext";width:win.adaptive?44:30;height:parent.height;text:"↓";onClicked:win.goTo(true) }
            Button { id:caseButton;objectName:"documentsFindCase";width:win.adaptive?44:34;height:parent.height;text:"Aa";checkable:true;onToggled:win.refreshMatches() }
            Button { id:wordButton;objectName:"documentsFindWord";width:win.adaptive?54:40;height:parent.height;text:"Mot";checkable:true;onToggled:win.refreshMatches() }
            Text { id:findCount;objectName:"documentsFindCount";height:parent.height;verticalAlignment:Text.AlignVCenter;color:Theme.inkMuted;font.pixelSize:11;text:"" }
        }
        Row {
            visible:win.replaceMode
            x:8;y:(win.adaptive?52:36);spacing:6;height:win.adaptive?40:28
            TextField {
                id:replaceField;objectName:"documentsReplace";width:Math.max(80,findBar.width-(win.adaptive?340:300));height:parent.height;placeholderText:"Remplacer par";selectByMouse:true
                Keys.onPressed:event=>{if(event.key===Qt.Key_Escape){win.closeFindFromEscape();event.accepted=true}}
            }
            Button { objectName:"documentsReplaceOne";height:parent.height;text:"Remplacer";onClicked:win.replaceCurrent() }
            Button { objectName:"documentsReplaceAll";height:parent.height;text:"Tout";onClicked:win.replaceEverything() }
        }
        Button { x:parent.width-(win.adaptive?48:34);y:4;width:win.adaptive?44:30;height:win.adaptive?40:28;text:"×";onClicked:win.closeFind() }
    }
    Flickable {
        id:tabStrip;objectName:"documentsTabStrip"
        visible:win.pinnedTabs.length>0 && win.connected && win.remote.activeServerId===win.recentServerId
        x:win.contentLeft+12
        y:pathInput.y+pathInput.height+8+(recoveryBanner.visible?recoveryBanner.height+6:0)+(findBar.visible?findBar.height+6:0)
        width:win.width-x-12;height:visible?36:0
        contentWidth:tabRow.implicitWidth;contentHeight:height
        clip:true;flickableDirection:Flickable.HorizontalFlick
        Row {
            id:tabRow;spacing:5
            Repeater {
                model:win.pinnedTabs
                delegate:Rectangle {
                    required property string modelData
                    width:Math.min(180,Math.max(96,tabName.implicitWidth+38))
                    height:32;radius:7
                    color:win.documents && win.documents.path===modelData?"#5590B9E9":"#26FFFFFF"
                    border.color:"#35FFFFFF"
                    Text {
                        id:tabName;x:8;anchors.verticalCenter:parent.verticalCenter
                        width:parent.width-30;elide:Text.ElideMiddle
                        text:parent.modelData.split("/").pop()+(win.documents && win.documents.path===parent.modelData && win.documents.dirty?" ●":"")
                        color:Theme.ink;font.pixelSize:11
                    }
                    MouseArea { anchors.fill:parent;anchors.rightMargin:24;onClicked:win.openRecent(parent.modelData) }
                    Text { anchors.right:parent.right;anchors.rightMargin:7;anchors.verticalCenter:parent.verticalCenter;text:"×";color:Theme.ink;font.pixelSize:15
                        MouseArea { anchors.fill:parent;onClicked:win.closePinnedTab(parent.modelData) }
                    }
                }
            }
        }
    }
    Flow {
        id:formatBar;objectName:"documentsFormatToolbar"
        visible:win.documents!==null && win.documents.hasDocument && !win.documents.pdfMode && !win.documents.busy && (!win.previewMode || win.split)
        x:win.contentLeft+12;y:tabStrip.y+tabStrip.height+4
        width:win.width-x-12;spacing:4
        Button { text:"H1";onClicked:win.insertBlock("# ") }
        Button { text:"H2";onClicked:win.insertBlock("## ") }
        Button { text:"B";font.bold:true;onClicked:win.wrapSelection("**","**",false) }
        Button { text:"I";font.italic:true;onClicked:win.wrapSelection("*","*",false) }
        Button { text:"• Liste";onClicked:win.insertBlock("- ") }
        Button { text:"1. Liste";onClicked:win.insertBlock("1. ") }
        Button { text:"❝";onClicked:win.insertBlock("> ") }
        Button { text:"Code";onClicked:win.wrapSelection("`","`",false) }
        Button { text:"Lien";onClicked:win.wrapSelection("[","](https://)",true) }
        Button { text:"Image";onClicked:win.imageInsertOpen=!win.imageInsertOpen }
        Button { text:"Rechercher";onClicked:win.openFind(false) }
    }
    Row {
        id:imageInsertRow;objectName:"documentsImageInsert"
        visible:win.imageInsertOpen && formatBar.visible
        x:win.contentLeft+12;y:formatBar.y+formatBar.height+4
        width:win.width-x-12;height:40;spacing:6
        TextField { id:imagePathField;width:Math.max(90,parent.width-110);height:38;placeholderText:"images/figure.png (sur le serveur)";selectByMouse:true }
        Button {
            text:"Insérer";height:38
            enabled:imagePathField.text.length>0
            onClicked:{
                const path=imagePathField.text.trim()
                const segments=path.split("/")
                if(path.length>256 || path.startsWith("/") || path.includes("\\") || path.includes(":") ||
                   path.includes("?") || path.includes("#") || segments.some(p=>!p||p==="."||p==="..") ||
                   !["png","jpg","jpeg","webp"].includes(path.split(".").pop().toLowerCase()))return
                const alt=editor.selectedText.replace(/\\u2029/g," ").replace(/[\\[\\]]/g,"")
                const at=editor.selectionStart, end=editor.selectionEnd
                editor.remove(at,end);editor.insert(at,"!["+alt+"]("+path+")")
                editor.forceActiveFocus();win.imageInsertOpen=false
                imagePathField.text=""
            }
        }
    }
    Item {
        id:body;x:win.contentLeft+12;y:(imageInsertRow.visible?imageInsertRow.y+imageInsertRow.height+4:formatBar.y+(formatBar.visible?formatBar.height+6:0));width:win.width-x-12;height:Math.max(32,win.height-y-80)
        visible:win.documents!==null && win.documents.hasDocument && !win.documents.pdfMode
        Flickable {
            id:editScroll;objectName:"documentsEditorScroll";visible:win.split || !win.previewMode
            width:win.split?(parent.width-8)/2:parent.width;height:parent.height
            clip:true;contentWidth:width;contentHeight:editor.contentHeight+24;boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            TextEdit {
                id:editor;objectName:"documentsEditor"
                x:10;y:8;width:parent.width-24;textFormat:TextEdit.PlainText;wrapMode:TextEdit.Wrap
                color:Theme.ink;selectionColor:Theme.accent;font.pixelSize:14;selectByMouse:true;persistentSelection:true
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
    Item {
        id:pdfView;objectName:"documentsPdfView"
        visible:win.documents!==null && win.documents.pdfMode
        x:win.contentLeft+12;y:pathInput.y+pathInput.height+8
        width:win.width-x-12;height:Math.max(100,win.height-y-85)
        property real zoom:1.0
        Row {
            id:pdfActions;spacing:8;height:38
            Button { text:"‹";enabled:win.documents && !win.documents.busy && win.documents.pdfPage>1;onClicked:win.documents.showPdfPage(win.documents.pdfPage-1) }
            Text { text:win.documents?(win.documents.pdfPage||"…")+" / "+win.documents.pdfPages:"";color:Theme.ink;anchors.verticalCenter:parent.verticalCenter }
            Button { text:"›";enabled:win.documents && !win.documents.busy && win.documents.pdfPage<win.documents.pdfPages;onClicked:win.documents.showPdfPage(win.documents.pdfPage+1) }
            Button { text:"−";onClicked:pdfView.zoom=Math.max(0.5,pdfView.zoom-0.25) }
            Button { text:Math.round(pdfView.zoom*100)+" %";onClicked:pdfView.zoom=1 }
            Button { text:"+";onClicked:pdfView.zoom=Math.min(3,pdfView.zoom+0.25) }
        }
        Flickable {
            y:pdfActions.height+4;width:parent.width;height:parent.height-y
            clip:true;boundsBehavior:Flickable.StopAtBounds
            contentWidth:Math.max(width,pdfImage.implicitWidth*pdfView.zoom)
            contentHeight:Math.max(height,pdfImage.implicitHeight*pdfView.zoom)
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            ScrollBar.horizontal:ScrollBar { policy:ScrollBar.AsNeeded }
            Image {
                id:pdfImage;objectName:"documentsPdfPage"
                source:win.documents?win.documents.pdfImage:""
                width:implicitWidth*pdfView.zoom;height:implicitHeight*pdfView.zoom
                fillMode:Image.PreserveAspectFit
                asynchronous:true
            }
        }
        Text {
            anchors.centerIn:parent;visible:win.documents && win.documents.busy
            color:Theme.ink;text:"Chargement de la page PDF…"
        }
    }
    Rectangle {
        id:conflictPanel;objectName:"documentsConflict";z:50
        visible:win.documents!==null && win.documents.conflictActive
        x:win.contentLeft+12;y:pathInput.y+pathInput.height+8;width:win.width-x-12;height:Math.max(120,win.height-y-12);radius:12;color:"#F2121C30";border.color:"#40FFFFFF"
        Text { x:14;y:10;width:parent.width-28;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:14;text:"Conflit : le fichier a changé sur le serveur" }
        Text { x:14;y:36;width:parent.width-28;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:12;text:win.documents?win.documents.conflictSummary:"" }
        Flickable {
            x:14;y:72;width:parent.width-28;height:parent.height-72-(win.adaptive?104:56);clip:true;contentWidth:width;contentHeight:diffText.implicitHeight+8;boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            Text { id:diffText;objectName:"documentsConflictDiff";width:parent.width;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:Theme.ink;font.family:"monospace";font.pixelSize:12;text:win.documents?win.documents.conflictDiff:"" }
        }
        Flow {
            x:14;anchors.bottom:parent.bottom;anchors.bottomMargin:10;width:parent.width-28;spacing:8
            Button { objectName:"documentsConflictCopy";enabled:win.documents!==null && !win.documents.conflictBusy && !win.documents.busy;text:"Enregistrer ma version comme copie";onClicked:win.documents.resolveConflictSaveCopy() }
            Button { objectName:"documentsConflictServer";enabled:win.documents!==null && !win.documents.conflictBusy && !win.documents.busy;text:"Charger la version du serveur";onClicked:win.documents.resolveConflictTakeServer() }
            Button { objectName:"documentsConflictClose";text:"Garder mon texte, décider plus tard";onClicked:win.documents.closeConflict() }
        }
    }
    Timer { id:previewTimer;interval:180;onTriggered:{preview.source=editor.text;win.stats=finder.stats(editor.text);win.updateOutline();win.refreshImageResources();if(win.findOpen)win.refreshMatches()} }
    Text {
        objectName:"documentsStats";visible:win.documents!==null && win.documents.hasDocument
        anchors.right:parent.right;anchors.rightMargin:12;anchors.bottom:parent.bottom;anchors.bottomMargin:10;color:Theme.inkMuted;font.pixelSize:10
        text:win.stats.words+" mots · "+win.stats.characters+" car. · "+win.stats.lines+" lignes"
    }
    Column {
        visible:!win.documents || !win.documents.hasDocument
        x:win.contentLeft+16;y:160;width:win.width-x-16;spacing:14
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"MDok · documents Markdown du serveur";color:Theme.ink;font.pixelSize:16 }
        Button { objectName:"documentsConnectButton";visible:!win.connected;anchors.horizontalCenter:parent.horizontalCenter;text:"Choisir un serveur…";onClicked:win.requestServers() }
        Text { width:parent.width;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap;text:"Ouvrez un Markdown ou PDF du serveur, ou saisissez son chemin ci-dessus.";color:Theme.inkMuted;font.pixelSize:12 }
    }
    Text { x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:34;width:win.width-x-12;height:42;wrapMode:Text.WordWrap;maximumLineCount:3;elide:Text.ElideRight;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:11;text:win.documents?(win.documents.hasDocument && !win.documents.sourceAvailable?"Source indisponible · texte conservé en mémoire. ":"")+win.documents.status+(win.documents.draftStatus!==""?"  ·  "+win.documents.draftStatus:""):"Choisissez un serveur Linux." }
    ToolButtonGlass { x:win.contentLeft+12;anchors.bottom:parent.bottom;anchors.bottomMargin:4;glyph:"⧉";tip:"Copier le brouillon dans le presse-papiers";enabledState:win.documents!==null && win.documents.hasDocument;onClicked:{const start=editor.selectionStart,end=editor.selectionEnd;editor.selectAll();editor.copy();editor.select(start,end)} }
    Text { x:win.contentLeft+50;anchors.bottom:parent.bottom;anchors.bottomMargin:10;visible:preview.limited;text:"Aperçu limité aux 64 000 premiers caractères";color:Theme.inkMuted;font.pixelSize:10 }
    Keys.onPressed:event=>{
        const ctrl=event.modifiers&Qt.ControlModifier
        if(ctrl && event.key===Qt.Key_S && win.documents && !win.documents.pdfMode){win.saveDocument();event.accepted=true}
        else if(!win.documents || !win.documents.hasDocument)return
        else if(ctrl && event.key===Qt.Key_F){win.openFind(false);event.accepted=true}
        else if(ctrl && event.key===Qt.Key_H){win.openFind(true);event.accepted=true}
        else if(event.key===Qt.Key_F3){win.findOpen=true;win.goTo(!(event.modifiers&Qt.ShiftModifier));event.accepted=true}
        else if(event.key===Qt.Key_Escape && win.findOpen){win.closeFindFromEscape();event.accepted=true}
        else if(ctrl && event.key===Qt.Key_B){win.wrapSelection("**","**",false);event.accepted=true}
        else if(ctrl && event.key===Qt.Key_I){win.wrapSelection("*","*",false);event.accepted=true}
        else if(ctrl && event.key===Qt.Key_K){win.wrapSelection("[","](https://)",true);event.accepted=true}
    }
    // A handled search Escape must not become a second Back action in the shell.
    Keys.onReleased:event=>{
        if(event.key===Qt.Key_Escape && win.escapeHandled){win.escapeHandled=false;event.accepted=true}
    }
    GlassDialog {
        id:confirm;objectName:"documentsUnsavedDialog";anchors.fill:parent;touchMode:win.adaptive
        onAccepted:if(danger)win.continuePending(true);else win.saveAndContinue()
        onAlternate:win.continuePending(true)
        onRejected:win.clearPending()
        Keys.onEscapePressed:event=>{event.accepted=true}
        Keys.onReleased:event=>{if(event.key===Qt.Key_Escape){event.accepted=true;if(!busy){visible=false;win.clearPending();editor.forceActiveFocus()}}}
    }
    Connections { target:win.documents
        function onMarkdownImageReady(path,base64){preview.setImageData(path,base64)}
        function onChanged(){
            if(win.waitingForSave && !win.documents.busy && win.documents.dirty){
                win.waitingForSave=false;confirm.busy=false;confirm.error=win.documents.status
            }
            if(win.documents.conflictActive && confirm.visible){confirm.visible=false;win.clearPending()}
        }
        function onLoaded(){win.syncDocument()}
        function onSaved(path){pathInput.text=path;win.rekeySavedTab(path);if(win.waitingForSave)win.continuePending(false)}
    }
}
