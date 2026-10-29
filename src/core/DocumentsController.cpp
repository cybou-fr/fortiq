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
#include <algorithm>
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
DocumentsController *DocumentsController::createTabSession(){
    auto *tab=new DocumentsController(m_remote,this);
    tab->m_code=m_code;
    m_tabSessions.append(tab);
    connect(tab,&DocumentsController::changed,this,&DocumentsController::changed);
    connect(tab,&QObject::destroyed,this,[this]{
        m_tabSessions.erase(std::remove_if(m_tabSessions.begin(),m_tabSessions.end(),
            [](const QPointer<DocumentsController> &p){return p.isNull();}),m_tabSessions.end());
        emit changed();
    });
    emit changed();
    return tab;
}
bool DocumentsController::hasDirtyTabs()const{
    if(dirty())return true;
    for(const auto &tab:m_tabSessions)if(tab && tab->dirty())return true;
    return false;
}
QVariantList DocumentsController::dirtyTabSessions()const{
    QVariantList result;
    if(dirty())result.append(QVariant::fromValue(static_cast<QObject *>(const_cast<DocumentsController *>(this))));
    for(const auto &tab:m_tabSessions)
        if(tab && tab->dirty())result.append(QVariant::fromValue(static_cast<QObject *>(tab.data())));
    return result;
}
bool DocumentsController::releaseTabSession(DocumentsController *session){
    if(!session||session==this||session->parent()!=this||session->dirty()||session->busy()||session->m_draftUploadPending)return false;
    for(auto &tab:m_tabSessions)if(tab==session){
        tab->deleteLater();
        tab=nullptr;
        emit changed();
        return true;
    }
    return false;
}
bool DocumentsController::hasBusyTabs()const{
    // Draft uploads are SSH writes too. They do not set m_busy, but must
    // block disconnect/exit exactly like a manual Save.
    if(busy() || m_draftUploadPending)return true;
    for(const auto &tab:m_tabSessions)
        if(tab && (tab->busy() || tab->m_draftUploadPending))return true;
    return false;
}

bool DocumentsController::acceptsPath(const QString &path)const{return m_code||markdownPath(path);}
bool DocumentsController::sourceAvailable()const{return m_hasDocument&&m_remote->connected()&&!m_remote->linkLost()&&m_sourceToken==m_remote->connectionToken()&&m_sourceServerId==m_remote->activeServerId();}
bool DocumentsController::canSave()const{return sourceAvailable()&&!m_unknown;}
bool DocumentsController::isCurrentDocument(const QString &path) const {
    const auto clean=cleanAbsolutePath(path);
    return clean && m_hasDocument && !m_new && !m_unknown && *clean==m_path && m_remote->connected()
        && m_sourceToken==m_remote->connectionToken() && m_sourceServerId==m_remote->activeServerId();
}
void DocumentsController::setText(const QString &text){if(m_busy || text==m_text)return;m_text=text;if(dirty())m_draftStatus="Modifications non encore sauvegardées sur le serveur.";emit changed();scheduleDraft();}
void DocumentsController::setAutosaveDelay(int ms){m_autosaveMs=qBound(0,ms,600000);}
void DocumentsController::discardDraft(){if(m_busy)return;m_draftTimer.stop();if(dirty()&&hasDocument()&&!m_new)deleteDraft();m_text=m_base;m_conflict=false;m_recovery.clear();emit changed();emit loaded();}
void DocumentsController::createNew(){
    if(m_busy || !m_remote->connected())return;
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant de changer de document.";emit changed();return;}
    ++m_seq;m_pdfMode=false;m_pdfImage.clear();m_pdfPage=m_pdfPages=0;m_sourceToken=m_remote->connectionToken();m_sourceLabel=m_remote->serverLabel();m_sourceServerId=m_remote->activeServerId();
    m_recovery.clear();m_conflict=false;m_draftTimer.stop();m_path.clear();m_base.clear();m_text="# Nouveau document\n";m_hash.clear();m_bom=m_crlf=m_unknown=false;m_new=m_hasDocument=true;
    m_status=m_code?"Nouveau fichier — choisissez un chemin absolu pour enregistrer.":"Nouveau Markdown — choisissez un chemin absolu .md pour enregistrer.";emit changed();emit loaded();
}
bool DocumentsController::recoverAsNew(DocumentsController *source){
    if(!source||source==this||source->m_remote!=m_remote||source->m_pdfMode||
       !source->m_hasDocument||source->m_busy||m_busy||dirty()||
       !m_remote->connected()||m_remote->linkLost())return false;
    const QString snapshot=source->m_text;
    const QByteArray encoded=encodeMarkdown(snapshot,false,false);
    if(encoded.size()>MarkdownLimit||snapshot.contains(QChar::Null))return false;
    ++m_seq;
    m_draftTimer.stop();
    m_pdfMode=false;m_pdfImage.clear();m_pdfPage=m_pdfPages=0;
    m_sourceToken=m_remote->connectionToken();m_sourceServerId=m_remote->activeServerId();
    m_sourceLabel=m_remote->serverLabel();
    m_path.clear();m_hash.clear();m_base.clear();
    m_text=snapshot;m_hasDocument=true;m_new=true;
    m_bom=false;m_crlf=false;m_unknown=false;
    m_recovery.clear();m_conflict=false;
    m_status="Copie de récupération en mémoire : choisissez un nouveau chemin .md et enregistrez.";
    emit changed();emit loaded();
    return true;
}
void DocumentsController::open(const QString &path){
    if(m_busy || !m_remote->connected())return;
    if(isCurrentDocument(path))return; // Re-activate without losing edits, selection or Undo.
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant d'ouvrir un autre document.";emit changed();return;}
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !acceptsPath(*clean)){m_status=m_code?"Chemin absolu requis.":"Chemin absolu .md ou .markdown requis.";emit changed();return;}
    m_pdfMode=false;m_pdfImage.clear();m_pdfPage=m_pdfPages=0;
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken(),seq=++m_seq;
    const auto label=m_remote->serverLabel();m_operationToken=token;m_busy=true;m_writing=false;m_status=m_code?"Lecture du fichier…":"Lecture du Markdown…";emit changed();
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
            m_sourceToken=token;m_sourceLabel=label;m_sourceServerId=m_remote->activeServerId();m_hasDocument=true;m_new=m_unknown=false;m_status=QString(m_code?"Fichier chargé":"Markdown chargé")+" · UTF-8 · "+QString(m_crlf?"CRLF":"LF");
            m_recovery.clear();m_conflict=false;m_draftStatus.clear();m_draftTimer.stop();
            emit changed();emit loaded();checkDraft();return;
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,target=*clean]{SshSession session;auto r=session.open(request);return r.status==SshResult::Status::Ok?session.readFile(target,MarkdownLimit):r;}));
}

void DocumentsController::openPdf(const QString &path){
    if(m_busy || !m_remote->connected() || m_remote->linkLost())return;
    if(dirty()){m_status="Enregistrez ou abandonnez les modifications avant d’ouvrir un PDF.";emit changed();return;}
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !clean->endsWith(".pdf",Qt::CaseInsensitive)){m_status="Chemin absolu .pdf requis.";emit changed();return;}
    const auto request=m_remote->connectionRequest();
    const int token=m_remote->connectionToken(),seq=++m_seq;
    const QString server=m_remote->activeServerId();
    m_operationToken=token;m_busy=true;m_writing=false;m_status="Lecture des informations PDF…";emit changed();
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,server,target=*clean]{
        const auto result=watcher->result();watcher->deleteLater();
        if(seq!=m_seq||!m_remote->connected()||m_remote->linkLost()||token!=m_remote->connectionToken()||server!=m_remote->activeServerId())return;
        m_busy=false;
        bool ok=false;
        const int pages=QString::fromUtf8(result.data).trimmed().toInt(&ok);
        if(result.status!=SshResult::Status::Ok || result.code!="exit:0" || result.truncated || !ok || pages<1 || pages>10000){
            m_status="Impossible de lire ce PDF (pdfinfo absent, fichier invalide ou trop grand).";emit changed();return;
        }
        m_pdfMode=true;m_path=target;m_pdfPages=pages;m_pdfPage=0;m_pdfImage.clear();
        m_hasDocument=true;m_new=false;m_unknown=false;m_base=m_text=QString();
        m_sourceToken=token;m_sourceServerId=server;m_sourceLabel=m_remote->serverLabel();
        m_status=QString("PDF · %1 page(s) · lecture seule").arg(pages);
        m_recovery.clear();m_conflict=false;m_draftTimer.stop();
        emit changed();emit loaded();
        renderPdfPage(target,1,pages);
    });
    const QByteArray script="LC_ALL=C pdfinfo " + SshSession::shellQuote(*clean)
        + " 2>/dev/null | awk '/^Pages:[[:space:]]+[0-9]+/{print $2; found=1; exit} END{if(!found)exit 2}'\n";
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{SshSession session;auto r=session.open(request);return r.status==SshResult::Status::Ok?session.runScript(script,128,{},15000):r;}));
}
void DocumentsController::showPdfPage(int page){
    if(!m_pdfMode||m_busy||!sourceAvailable()||page<1||page>m_pdfPages)return;
    renderPdfPage(m_path,page,m_pdfPages);
}
void DocumentsController::renderPdfPage(const QString &path,int page,int pages){
    const auto request=m_remote->connectionRequest();
    const int token=m_remote->connectionToken(),seq=++m_seq;
    const QString server=m_sourceServerId;
    m_operationToken=token;m_busy=true;m_writing=false;m_status=QString("Rendu PDF · page %1/%2…").arg(page).arg(pages);emit changed();
    const QByteArray file=SshSession::shellQuote(path);
    const QByteArray number=QByteArray::number(page);
    const QByteArray script=
        "umask 077\n"
        "command -v pdftoppm >/dev/null 2>&1 || exit 12\n"
        "dir=$(mktemp -d) || exit 13\n"
        "trap 'rm -rf \"$dir\"' EXIT HUP INT TERM\n"
        "pdftoppm -f "+number+" -l "+number+" -singlefile -scale-to 1600 -png "+file+" \"$dir/page\" >/dev/null 2>&1 || exit 14\n"
        "test -f \"$dir/page.png\" || exit 15\n"
        "bytes=$(wc -c < \"$dir/page.png\")\n"
        "test \"$bytes\" -le 3000000 || exit 16\n"
        "base64 -w0 \"$dir/page.png\"\n";
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,server,path,page,pages]{
        const auto result=watcher->result();watcher->deleteLater();
        if(seq!=m_seq||!m_remote->connected()||m_remote->linkLost()||token!=m_remote->connectionToken()||server!=m_remote->activeServerId()||path!=m_path||!m_pdfMode)return;
        m_busy=false;
        const QByteArray png=QByteArray::fromBase64(result.data.trimmed());
        if(result.status!=SshResult::Status::Ok||result.code!="exit:0"||result.truncated||
           png.size()>3000000||!png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))){
            m_status="Rendu PDF impossible : vérifiez pdftoppm et les permissions.";emit changed();return;
        }
        m_pdfImage=QStringLiteral("data:image/png;base64,")+QString::fromLatin1(result.data.trimmed());
        m_pdfPage=page;m_pdfPages=pages;
        m_status=QString("PDF · page %1/%2 · lecture seule").arg(page).arg(pages);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{SshSession session;auto r=session.open(request);return r.status==SshResult::Status::Ok?session.runScript(script,4100000,{},30000):r;}));
}

void DocumentsController::exportPdf(const QString &destination){
    if(m_pdfMode||!m_hasDocument||m_busy||!canSave())return;
    const auto clean=cleanAbsolutePath(destination);
    if(!clean||!clean->endsWith(".pdf",Qt::CaseInsensitive)){
        m_status="Destination PDF absolue requise.";emit changed();return;
    }
    const QByteArray markdown=encodeMarkdown(m_text,m_bom,m_crlf);
    if(markdown.size()>MarkdownLimit||markdown.contains('\0')){
        m_status="Document Markdown trop grand ou invalide.";emit changed();return;
    }
    const QString parent=parentPath(*clean);
    const QByteArray folder=SshSession::shellQuote(parent);
    const QByteArray output=SshSession::shellQuote(*clean);
    // The Markdown travels only as base64 in a quoted here-document. Pandoc is
    // invoked without shell interpolation of document contents or destination.
    const QByteArray delimiter="FQMD"+QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    const QByteArray script=
        "umask 077\n"
        "command -v pandoc >/dev/null 2>&1 || { echo 'pandoc missing' >&2; exit 12; }\n"
        "command -v weasyprint >/dev/null 2>&1 || { echo 'weasyprint missing' >&2; exit 12; }\n"
        "dir="+folder+"\n"
        "test -d \"$dir\" || exit 13\n"
        "test ! -e "+output+" && test ! -L "+output+" || exit 17\n"
        "tmp=$(mktemp -d \"$dir/.fortiq-pdf.XXXXXXXX\") || exit 14\n"
        "trap 'rm -rf \"$tmp\"' EXIT HUP INT TERM\n"
        "base64 -d > \"$tmp/input.md\" <<'"+delimiter+"'\n"+
        markdown.toBase64()+"\n"+delimiter+"\n"+
        "pandoc --from=gfm --standalone --pdf-engine=weasyprint "
        "--resource-path=\"$tmp\" \"$tmp/input.md\" -o \"$tmp/output.pdf\" >/dev/null 2>&1 || exit 15\n"
        "test -s \"$tmp/output.pdf\" || exit 16\n"
        "test \"$(head -c 5 \"$tmp/output.pdf\")\" = '%PDF-' || exit 16\n"
        "ln \"$tmp/output.pdf\" "+output+" || exit 17\n"
        "echo '#exported'\n";
    const auto request=m_remote->connectionRequest();
    const int token=m_remote->connectionToken(),seq=++m_seq;
    const QString server=m_sourceServerId;
    m_operationToken=token;m_busy=true;m_writing=true;
    m_status="Export PDF sur le serveur…";emit changed();
    const int taskId=beginDocTask(m_remote,QStringLiteral("docExportPdf"),
                                 clean->section('/',-1),*clean,false);
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,server,target=*clean,taskId]{
        const auto result=watcher->result();watcher->deleteLater();
        if(seq!=m_seq||!m_remote->connected()||m_remote->linkLost()||
           token!=m_remote->connectionToken()||server!=m_remote->activeServerId()){
            endDocTask(m_remote,taskId,"unknown",QStringLiteral("Session changée pendant l'export PDF."));
            return;
        }
        m_busy=false;
        if(result.status==SshResult::Status::Ok&&result.code=="exit:0"&&
           !result.truncated&&result.data.contains("#exported")){
            endDocTask(m_remote,taskId,"ok",target);
            m_status="PDF exporté : "+target;
            if(m_remote->currentDirectory()==parentPath(target)&&!m_remote->busy())m_remote->refresh();
        }else{
            const bool unknown=result.code=="unknown"||result.code=="timeout";
            endDocTask(m_remote,taskId,unknown?"unknown":"fail",
                       result.message.isEmpty()?QStringLiteral("Export PDF non confirmé."):result.message,result.code);
            m_status=unknown?"Export PDF incertain : vérifiez le serveur avant de réessayer."
                            :"Export PDF impossible : installez pandoc et weasyprint et vérifiez le chemin.";
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,script]{
        SshSession session;auto result=session.open(request);
        return result.status==SshResult::Status::Ok?session.runScript(script,4096,{},120000):result;
    }));
}

void DocumentsController::requestMarkdownImage(const QString &relativePath){
    if(m_pdfMode||m_new||m_path.isEmpty()||!sourceAvailable()||relativePath.isEmpty()||relativePath.size()>256)return;
    if(relativePath.startsWith('/')||relativePath.startsWith('\\')||relativePath.contains('\\')||
       relativePath.contains('?')||relativePath.contains('#')||relativePath.contains(':')||
       relativePath.contains(QChar::Null))return;
    const QStringList components=relativePath.split('/');
    for(const QString &part:components)if(part.isEmpty()||part=="."||part=="..")return;
    const QString lower=relativePath.toLower();
    if(!(lower.endsWith(".png")||lower.endsWith(".jpg")||lower.endsWith(".jpeg")||lower.endsWith(".webp")))return;
    const auto target=cleanAbsolutePath(parentPath(m_path)+"/"+relativePath);
    if(!target)return;
    const QString path=m_path, server=m_sourceServerId;
    const int token=m_remote->connectionToken(),epoch=m_seq;
    const auto request=m_remote->connectionRequest();
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,path,server,token,epoch,relativePath]{
        const auto result=watcher->result();watcher->deleteLater();
        if(epoch!=m_seq||path!=m_path||!sourceAvailable()||token!=m_remote->connectionToken()||
           server!=m_remote->activeServerId()||result.status!=SshResult::Status::Ok||result.truncated||
           result.data.isEmpty()||result.data.size()>2097152)return;
        emit markdownImageReady(relativePath,QString::fromLatin1(result.data.toBase64()));
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,target=*target,relativePath,base=parentPath(m_path)]{
        SshSession session;auto r=session.open(request);
        if(r.status!=SshResult::Status::Ok)return r;
        // Reject symlinks in every relative component. Non-atomic with SFTP
        // read: this is a mitigation, not a sandbox against concurrent renames.
        QString item=base;
        QByteArray guard="test -d "+SshSession::shellQuote(base)+" || exit 11\n";
        for(const QString &part:relativePath.split('/')){
            item+="/"+part;
            guard+="test ! -L "+SshSession::shellQuote(item)+" || exit 12\n";
        }
        guard+="echo checked\n";
        const auto check=session.runScript(guard,128,{},10000);
        if(check.status!=SshResult::Status::Ok||check.code!="exit:0"||!check.data.contains("checked"))
            return SshResult{};
        return session.readFile(target,2097152);
    }));
}
void DocumentsController::save(){if(!m_pdfMode && !m_new)write(m_path,false);}
void DocumentsController::saveAs(const QString &path){if(!m_pdfMode)write(path,true);}
void DocumentsController::write(const QString &path,bool copy){
    if(m_pdfMode||!canSave())return;
    if(!fileSaveCanStart(m_busy,m_draftUploadPending)){
        if(m_draftUploadPending && !m_busy){
            // This is a user-requested Save, not an automatic retry of an unknown write.
            // Replace an earlier pending intent with the latest explicit choice.
            m_pendingSavePath=path;
            m_pendingSaveCopy=copy;
            m_pendingSaveEpoch=m_seq;
            m_status="Enregistrement du brouillon en cours ; Save sera lancé ensuite.";
            emit changed();
        }
        return;
    }
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !acceptsPath(*clean)){m_status=m_code?"Destination absolue requise.":"Destination absolue .md ou .markdown requise.";emit changed();return;}
    const auto bytes=encodeMarkdown(m_text,m_bom,m_crlf);
    if(bytes.size()>MarkdownLimit || m_text.contains(QChar::Null)){m_status="Document supérieur à 256 Kio ou caractère NUL.";emit changed();return;}
    m_draftTimer.stop();
    const auto request=m_remote->connectionRequest();const auto hash=m_hash;const auto text=m_text;const QString oldPath=m_path;
    const int token=m_remote->connectionToken(),seq=++m_seq;m_operationToken=token;m_busy=true;m_writing=true;m_status="Enregistrement sûr en cours…";emit changed();
    const int taskId=beginDocTask(m_remote,copy?QStringLiteral("docSaveCopy"):QStringLiteral("docSave"),clean->section('/',-1),*clean,false);
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,text,taskId,copy,oldPath]{
        const auto result=watcher->result();watcher->deleteLater();if(seq!=m_seq || !m_remote->connected() || m_remote->linkLost() || token!=m_remote->connectionToken()){
            endDocTask(m_remote,taskId,"unknown",QStringLiteral("Session changée pendant l'enregistrement : résultat inconnu."));
            if(seq==m_seq){
                // The server may have committed the write before SSH disappeared.
                // Preserve the local buffer, do not advance the base hash, and
                // never retry this operation without explicit user recovery.
                m_busy=false;m_writing=false;m_unknown=true;
                m_draftTimer.stop();
                m_pendingSavePath.clear();m_pendingSaveEpoch=-1;
                m_status="Résultat de l'enregistrement inconnu après perte SSH. Texte conservé en mémoire ; vérifiez le fichier serveur avant toute nouvelle écriture.";
                emit changed();
            }
            return;
        }
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
    if(m_busy || m_draftUploadPending){m_draftTimer.start(1000);return;} // coalesce writes; a newer snapshot must never be overtaken
    const auto bytes=encodeMarkdown(m_text,m_bom,m_crlf);
    if(bytes.size()>MarkdownLimit||m_text.contains(QChar::Null))return;
    DraftMeta meta;meta.path=m_path;meta.baseHashHex=QString::fromLatin1(m_hash.toHex());meta.savedAtSecs=QDateTime::currentSecsSinceEpoch();meta.bom=m_bom;meta.crlf=m_crlf;
    const auto script=buildDraftWriteScript(draftId(m_path),draftMetaText(meta),bytes,QUuid::createUuid().toString(QUuid::Id128).left(12));
    if(script.isEmpty())return;
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken();const QString path=m_path;
    const QString serverId=m_sourceServerId;
    const int documentEpoch=m_seq;
    const QString snapshot=m_text;
    m_draftUploadPending=true;
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,token,path,serverId,documentEpoch,snapshot]{
        const auto r=watcher->result();watcher->deleteLater();
        m_draftUploadPending=false;
        // Take the user's intent before the old completion can touch a new document.
        const bool runPending=m_pendingSaveEpoch==m_seq && !m_pendingSavePath.isEmpty();
        const QString pendingPath=m_pendingSavePath;
        const bool pendingCopy=m_pendingSaveCopy;
        m_pendingSavePath.clear();m_pendingSaveEpoch=-1;
        if(!draftCompletionBelongsToDocument(path,m_path,token,m_remote->connectionToken(),
                                              serverId,m_remote->activeServerId(),m_remote->connected()&&!m_remote->linkLost(),
                                              documentEpoch,m_seq)){
            // A draft may have reached the server before SSH was lost.
            // Never requeue an unacknowledged write or continue a pending Save.
            if(documentEpoch==m_seq && path==m_path && serverId==m_sourceServerId){
                m_draftTimer.stop();
                m_draftStatus="Résultat du brouillon inconnu après changement SSH ; texte local conservé. Vérifiez le serveur avant de réessayer.";
                if(runPending)m_status="Save différé annulé : session SSH changée ; résultat du brouillon inconnu.";
                emit changed();
            }
            return;
        }
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
        // Only after a confirmed draft acknowledgement, and only in the same
        // document epoch, consume the explicit Save request exactly once.
        // No replay after an unknown/failed draft operation or lost connection.
        if(pendingManualSaveMayRun(runPending,success,canSave(),m_unknown))write(pendingPath,pendingCopy);
        else if(runPending){
            m_status="Save en attente annulé : brouillon non confirmé ou source indisponible. Vérifiez puis réessayez.";
            emit changed();
        }
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
    const QString serverId=m_sourceServerId;
    const int documentEpoch=m_seq;
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,token,path,serverId,documentEpoch]{
        const auto r=watcher->result();watcher->deleteLater();
        if(!draftCompletionBelongsToDocument(path,m_path,token,m_remote->connectionToken(),
                                             serverId,m_remote->activeServerId(),m_remote->connected()&&!m_remote->linkLost(),
                                             documentEpoch,m_seq) || dirty() || r.status!=SshResult::Status::Ok)return;
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
    const QString copyName=(dot>0?name.left(dot):name)+" (ma version)"+(dot>0?name.mid(dot):(m_code?QString():QString(".md")));
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
