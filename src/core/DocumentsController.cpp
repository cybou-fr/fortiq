#include "DocumentsController.h"
#include "MarkdownData.h"
#include "RemoteController.h"
#include "RemotePath.h"
#include "TaskManager.h"
#include "DocumentDraft.h"
#include "../ssh/SshClient.h"
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QCryptographicHash>
#include <QDateTime>
#include <QUuid>
namespace fortiq {
namespace {
// Opening is a read (transient, nothing to verify after a session change); saving is a write that guards the session and may end "unknown".
int beginDocTask(RemoteController *remote, const QString &kind, const QString &title, const QString &destination, bool read) {
    TaskManager *tasks = remote->tasks();
    if (!tasks) return 0;
    TaskManager::Spec spec;
    spec.kind = kind; spec.title = title; spec.source = remote->serverLabel(); spec.destination = destination;
    spec.generation = remote->connectionToken(); spec.cancellable = false;
    spec.guards = !read; spec.session = read; spec.transient = read;
    return tasks->begin(spec);
}
void endDocTask(RemoteController *remote, int id, const char *how, const QString &message, const QString &code = {}) {
    TaskManager *tasks = remote->tasks();
    if (!tasks || id == 0) return;
    const QString h = QString::fromLatin1(how);
    if (h == "ok") tasks->succeed(id, message);
    else if (h == "unknown") tasks->markUnknown(id, message);
    else if (h == "cancelled") tasks->markCancelled(id, message);
    else tasks->fail(id, message, code);
}
}
DocumentsController::DocumentsController(RemoteController *remote,QObject *parent):QObject(parent),m_remote(remote) {
    m_pool.setMaxThreadCount(1);
    m_draftTimer.setSingleShot(true);
    connect(&m_draftTimer,&QTimer::timeout,this,[this]{writeDraftNow();});
    connect(remote,&RemoteController::linkChanged,this,&DocumentsController::changed);
    connect(remote,&RemoteController::changed,this,[this]{
        const int token=m_remote->connected()?m_remote->connectionToken():-1;
        if(m_busy ? m_operationToken!=token : (m_sourceToken>=0 && m_sourceToken!=token)){
            if(m_busy && m_writing)m_unknown=true;
            ++m_seq;m_busy=false;m_status=m_unknown?"Enregistrement interrompu : résultat inconnu, aucun rejeu. Brouillon conservé en RAM.":"Source déconnectée ou changée. Brouillon conservé en RAM ; enregistrement désactivé.";emit changed();
        }
    });
}
DocumentsController::~DocumentsController(){m_pool.waitForDone();}
bool DocumentsController::sourceAvailable()const{return m_hasDocument&&m_remote->connected()&&!m_remote->linkLost()&&m_sourceToken==m_remote->connectionToken()&&m_sourceServerId==m_remote->activeServerId();}
bool DocumentsController::canSave()const{return sourceAvailable()&&!m_unknown;}
bool DocumentsController::isCurrentDocument(const QString &path) const {
    const auto clean=cleanAbsolutePath(path);
    return clean && m_hasDocument && !m_new && !m_unknown && *clean==m_path && m_remote->connected()
        && m_sourceToken==m_remote->connectionToken() && m_sourceServerId==m_remote->activeServerId();
}
void DocumentsController::setText(const QString &text){if(m_busy || text==m_text)return;m_text=text;emit changed();scheduleDraft();}
void DocumentsController::setAutosaveDelay(int ms){m_autosaveMs=qBound(0,ms,600000);}
void DocumentsController::discardDraft(){if(m_busy)return;m_draftTimer.stop();if(dirty()&&hasDocument()&&!m_new)deleteDraft();m_text=m_base;m_conflict=false;m_recovery.clear();emit changed();emit loaded();}
void DocumentsController::createNew(){
    if(m_busy || !m_remote->connected())return;
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant de changer de document.";emit changed();return;}
    ++m_seq;m_sourceToken=m_remote->connectionToken();m_sourceLabel=m_remote->serverLabel();m_sourceServerId=m_remote->activeServerId();
    m_recovery.clear();m_conflict=false;m_draftTimer.stop();m_path.clear();m_base.clear();m_text="# Nouveau document\n";m_hash.clear();m_bom=m_crlf=m_unknown=false;m_new=m_hasDocument=true;
    m_status="Nouveau Markdown — choisissez un chemin absolu .md pour enregistrer.";emit changed();emit loaded();
}
void DocumentsController::open(const QString &path){
    if(m_busy || !m_remote->connected())return;
    if(isCurrentDocument(path))return; // Re-activate without losing edits, selection or Undo.
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant d'ouvrir un autre document.";emit changed();return;}
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !markdownPath(*clean)){m_status="Chemin absolu .md ou .markdown requis.";emit changed();return;}
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken(),seq=++m_seq;
    const auto label=m_remote->serverLabel();m_operationToken=token;m_busy=true;m_writing=false;m_status="Lecture du Markdown…";emit changed();
    const int taskId=beginDocTask(m_remote,QStringLiteral("docOpen"),clean->section('/',-1),*clean,true);
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,target=*clean,label,taskId]{
        const auto result=watcher->result();watcher->deleteLater();if(seq!=m_seq || !m_remote->connected() || token!=m_remote->connectionToken()){endDocTask(m_remote,taskId,"cancelled",QStringLiteral("Session changée."));return;}
        m_busy=false;
        const auto decoded=decodeMarkdown(result.data,result.truncated);
        endDocTask(m_remote,taskId,(result.status==SshResult::Status::Ok && decoded.error.isEmpty())?"ok":"fail",result.status==SshResult::Status::Ok?decoded.error:result.message);
        if(result.status!=SshResult::Status::Ok || !decoded.error.isEmpty())m_status=result.status==SshResult::Status::Ok?decoded.error:result.message;
        else {
            m_path=target;m_text=m_base=decoded.text;m_bom=decoded.bom;m_crlf=decoded.crlf;m_hash=QCryptographicHash::hash(result.data,QCryptographicHash::Sha256);
            m_sourceToken=token;m_sourceLabel=label;m_sourceServerId=m_remote->activeServerId();m_hasDocument=true;m_new=m_unknown=false;m_status="Markdown chargé · UTF-8 · "+QString(m_crlf?"CRLF":"LF");
            m_recovery.clear();m_conflict=false;m_draftStatus.clear();m_draftTimer.stop();
            emit changed();emit loaded();checkDraft();return;
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,target=*clean]{SshSession session;auto r=session.open(request);return r.status==SshResult::Status::Ok?session.readFile(target,MarkdownLimit):r;}));
}
void DocumentsController::save(){if(!m_new)write(m_path,false);}
void DocumentsController::saveAs(const QString &path){write(path,true);}
void DocumentsController::write(const QString &path,bool copy){
    if(m_busy || !canSave())return;
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !markdownPath(*clean)){m_status="Destination absolue .md ou .markdown requise.";emit changed();return;}
    const auto bytes=encodeMarkdown(m_text,m_bom,m_crlf);
    if(bytes.size()>MarkdownLimit || m_text.contains(QChar::Null)){m_status="Document supérieur à 256 Kio ou caractère NUL.";emit changed();return;}
    m_draftTimer.stop();
    const auto request=m_remote->connectionRequest();const auto hash=m_hash;const auto text=m_text;const QString oldPath=m_path;
    const int token=m_remote->connectionToken(),seq=++m_seq;m_operationToken=token;m_busy=true;m_writing=true;m_status="Enregistrement sûr en cours…";emit changed();
    const int taskId=beginDocTask(m_remote,copy?QStringLiteral("docSaveCopy"):QStringLiteral("docSave"),clean->section('/',-1),*clean,false);
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,text,taskId,copy,oldPath]{
        const auto result=watcher->result();watcher->deleteLater();if(seq!=m_seq || !m_remote->connected() || token!=m_remote->connectionToken()){endDocTask(m_remote,taskId,"unknown",QStringLiteral("Session changée pendant l'enregistrement : résultat inconnu."));return;}
        m_busy=false;
        if(result.status==SshResult::Status::Ok){endDocTask(m_remote,taskId,"ok",result.directory);m_path=result.directory;m_base=m_text=text;m_hash=result.data;m_new=false;m_status="Enregistré : "+m_path;m_conflict=false;m_recovery.clear();
            if(!oldPath.isEmpty())deleteDraftFor(oldPath);if(m_path!=oldPath)deleteDraftFor(m_path);   // what was saved no longer needs its safety copy
            // Refresh only the folder still being viewed; never replace a newer navigation.
            if(m_remote->currentDirectory()==parentPath(m_path) && !m_remote->busy())m_remote->refresh();
            emit changed();emit saved(m_path);}
        else {endDocTask(m_remote,taskId,result.code=="unknown"?"unknown":"fail",result.code=="conflict"?QStringLiteral("Conflit : la version du serveur a changé."):result.message,result.code);m_unknown=result.code=="unknown";m_status=result.code=="conflict"?"Conflit : la version du serveur a changé. Votre texte est conservé (brouillon sur le serveur) ; comparez ou enregistrez une copie.":m_unknown?"Résultat inconnu. Aucun rejeu : vérifiez le fichier serveur. Brouillon conservé.":result.message;emit changed();
            if(result.code=="conflict"&&!copy)beginConflict();}
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,target=*clean,bytes,hash,copy]{
        SshSession session;auto r=session.open(request);if(r.status!=SshResult::Status::Ok)return r;
        return copy?session.saveCopy(parentPath(target),target.section('/',-1),bytes):session.saveFile(target,bytes,hash,false);
    }));
}

// ---------------------------------------------------------------------------------------------------------------- server drafts
void DocumentsController::scheduleDraft(){
    if(!m_hasDocument||m_new||m_path.isEmpty()||!dirty()||m_unknown||!canSave())return;
    m_draftTimer.start(m_autosaveMs);
}
void DocumentsController::writeDraftNow(){
    if(!m_hasDocument||m_new||m_path.isEmpty()||!dirty()||m_unknown||!canSave())return;
    if(m_busy){m_draftTimer.start(1000);return;}                                       // a save or an open is running: try again shortly
    const auto bytes=encodeMarkdown(m_text,m_bom,m_crlf);
    if(bytes.size()>MarkdownLimit||m_text.contains(QChar::Null))return;
    DraftMeta meta;meta.path=m_path;meta.baseHashHex=QString::fromLatin1(m_hash.toHex());meta.savedAtSecs=QDateTime::currentSecsSinceEpoch();meta.bom=m_bom;meta.crlf=m_crlf;
    const auto script=buildDraftWriteScript(draftId(m_path),draftMetaText(meta),bytes,QUuid::createUuid().toString(QUuid::Id128).left(12));
    if(script.isEmpty())return;
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken();const QString path=m_path;
    const QString snapshot=m_text;
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,token,path,snapshot]{
        const auto r=watcher->result();watcher->deleteLater();
        if(path!=m_path||!m_remote->connected()||token!=m_remote->connectionToken())return;
        const bool success=r.status==SshResult::Status::Ok&&r.code=="exit:0";
        // A completed SSH write only protects the snapshot sent, not edits made
        // while the remote operation was in flight. Never claim those edits are saved.
        if(success && draftNeedsAnotherSave(snapshot,m_text,dirty(),m_unknown)){
            m_draftStatus="Brouillon précédent enregistré ; nouvelles modifications en attente.";
            scheduleDraft();
        }else{
            m_draftStatus=success?"Brouillon gardé sur le serveur à "+QDateTime::currentDateTime().toString("HH:mm:ss")
                                 :"Brouillon serveur non enregistré ("+(r.message.isEmpty()?r.code:r.message.left(80))+") : le texte n'est que dans la mémoire du client.";
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{SshSession session;auto o=session.open(request);if(o.status!=SshResult::Status::Ok)return o;return session.runScript(script,4096);}));
}
void DocumentsController::deleteDraftFor(const QString &path){
    if(path.isEmpty()||!m_remote->connected()||m_sourceToken!=m_remote->connectionToken())return;
    const auto script=buildDraftDeleteScript(draftId(path));if(script.isEmpty())return;
    const auto request=m_remote->connectionRequest();
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,watcher,&QObject::deleteLater);
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{SshSession session;auto o=session.open(request);if(o.status!=SshResult::Status::Ok)return o;return session.runScript(script,1024);}));
}
void DocumentsController::deleteDraft(){deleteDraftFor(m_path);}
void DocumentsController::checkDraft(){
    if(!m_hasDocument||m_new||m_path.isEmpty()||!m_remote->connected())return;
    const auto script=buildDraftReadScript(draftId(m_path));if(script.isEmpty())return;
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken();const QString path=m_path;
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,token,path]{
        const auto r=watcher->result();watcher->deleteLater();
        if(path!=m_path||dirty()||!m_remote->connected()||token!=m_remote->connectionToken()||r.status!=SshResult::Status::Ok)return;
        const DraftRead draft=parseDraftRead(r.data);
        if(!draft.found||draft.meta.path!=path)return;
        const MarkdownData decoded=decodeMarkdown(draft.documentBytes);
        if(!decoded.error.isEmpty())return;
        if(decoded.text==m_text){deleteDraft();return;}                                  // the draft equals the file: nothing to recover
        const DiffSummary d=lineDiff(m_text,decoded.text);
        m_recoveryText=decoded.text;m_recoveryBase=draft.meta.baseHashHex;
        m_recovery={{"available",true},{"savedAt",QDateTime::fromSecsSinceEpoch(draft.meta.savedAtSecs).toString("dd.MM.yyyy HH:mm")},
                    {"baseMatches",draft.meta.baseHashHex==QString::fromLatin1(m_hash.toHex())},{"changedLines",d.onlyServer+d.onlyMine}};
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{SshSession session;auto o=session.open(request);if(o.status!=SshResult::Status::Ok)return o;return session.runScript(script,600000);}));
}
void DocumentsController::restoreDraft(){
    if(m_busy||dirty()||!m_recovery.value("available").toBool())return;
    m_text=m_recoveryText;
    const bool matches=m_recoveryBase==QString::fromLatin1(m_hash.toHex());
    if(!matches)m_hash=QByteArray::fromHex(m_recoveryBase.toLatin1());                   // the draft was written over an older version: saving it must meet the conflict check
    m_status=matches?"Brouillon restauré depuis le serveur.":"Brouillon restauré. Le fichier a changé depuis : l'enregistrement proposera de comparer les deux versions.";
    m_recovery.clear();m_recoveryText.clear();emit changed();emit loaded();scheduleDraft();
}
void DocumentsController::dismissRecovery(bool deleteIt){
    if(deleteIt&&m_recovery.value("available").toBool())deleteDraft();
    m_recovery.clear();m_recoveryText.clear();emit changed();
}

// ----------------------------------------------------------------------------------------------------------------- conflict screen
void DocumentsController::beginConflict(){
    m_conflict=true;m_conflictBusy=true;m_conflictDiff.clear();m_conflictServerText.clear();m_conflictServerHash.clear();
    m_conflictSummary="Comparaison avec la version du serveur…";emit changed();
    writeDraftNow();                                                                     // my text is safe on the server before anything else happens
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken();const QString path=m_path;
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,token,path]{
        const auto r=watcher->result();watcher->deleteLater();
        if(!m_conflict||path!=m_path||!m_remote->connected()||token!=m_remote->connectionToken())return;
        m_conflictBusy=false;
        const MarkdownData decoded=r.status==SshResult::Status::Ok?decodeMarkdown(r.data,r.truncated):MarkdownData{};
        if(r.status!=SshResult::Status::Ok||!decoded.error.isEmpty()){
            m_conflictSummary="La version du serveur n'a pas pu être lue : "+(r.status!=SshResult::Status::Ok?r.message:decoded.error);emit changed();return;
        }
        m_conflictServerText=decoded.text;m_conflictServerHash=QCryptographicHash::hash(r.data,QCryptographicHash::Sha256);m_conflictBom=decoded.bom;m_conflictCrlf=decoded.crlf;
        const DiffSummary d=lineDiff(m_text,decoded.text);
        m_conflictSummary=d.tooLong?d.text:QString("%1 ligne(s) seulement chez vous (−), %2 ligne(s) seulement sur le serveur (+).").arg(d.onlyMine).arg(d.onlyServer);
        m_conflictDiff=d.tooLong?QString():d.text;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,path]{SshSession session;auto o=session.open(request);return o.status==SshResult::Status::Ok?session.readFile(path,MarkdownLimit):o;}));
}
void DocumentsController::resolveConflictSaveCopy(){
    if(!m_conflict||m_busy||m_conflictBusy||m_path.isEmpty())return;
    const QString name=m_path.section('/',-1);const qsizetype dot=name.lastIndexOf('.');
    const QString copyName=(dot>0?name.left(dot):name)+" (ma version)"+(dot>0?name.mid(dot):QString(".md"));
    write(parentPath(m_path)+(parentPath(m_path).endsWith('/')?"":"/")+copyName,true);
}
void DocumentsController::resolveConflictTakeServer(){
    if(!m_conflict||m_busy||m_conflictBusy||m_conflictServerText.isNull()||m_conflictServerHash.isEmpty())return;
    writeDraftNow();                                                                     // queued first (single-thread pool): my version stays on the server
    m_text=m_base=m_conflictServerText;m_hash=m_conflictServerHash;m_bom=m_conflictBom;m_crlf=m_conflictCrlf;
    m_conflict=false;m_conflictDiff.clear();m_conflictServerText.clear();
    m_status="Version du serveur chargée. Votre texte reste sur le serveur (brouillon) : il sera proposé à la prochaine ouverture.";
    emit changed();emit loaded();
}
void DocumentsController::closeConflict(){if(!m_conflict)return;m_conflict=false;emit changed();}
}
