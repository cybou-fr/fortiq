#include "DocumentsController.h"
#include "MarkdownData.h"
#include "RemoteController.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QCryptographicHash>
namespace fortiq {
DocumentsController::DocumentsController(RemoteController *remote,QObject *parent):QObject(parent),m_remote(remote) {
    m_pool.setMaxThreadCount(1);
    connect(remote,&RemoteController::changed,this,[this]{
        const int token=m_remote->connected()?m_remote->connectionToken():-1;
        if(m_busy ? m_operationToken!=token : (m_sourceToken>=0 && m_sourceToken!=token)){
            if(m_busy && m_writing)m_unknown=true;
            ++m_seq;m_busy=false;m_status=m_unknown?"Enregistrement interrompu : résultat inconnu, aucun rejeu. Brouillon conservé en RAM.":"Source déconnectée ou changée. Brouillon conservé en RAM ; enregistrement désactivé.";emit changed();
        }
    });
}
DocumentsController::~DocumentsController(){m_pool.waitForDone();}
bool DocumentsController::canSave()const{return m_hasDocument&&!m_unknown&&m_remote->connected()&&m_sourceToken==m_remote->connectionToken();}
void DocumentsController::setText(const QString &text){if(m_busy || text==m_text)return;m_text=text;emit changed();}
void DocumentsController::discardDraft(){if(m_busy)return;m_text=m_base;emit changed();emit loaded();}
void DocumentsController::createNew(){
    if(m_busy || !m_remote->connected())return;
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant de changer de document.";emit changed();return;}
    ++m_seq;m_sourceToken=m_remote->connectionToken();m_sourceLabel=m_remote->serverLabel();
    m_path.clear();m_base.clear();m_text="# Nouveau document\n";m_hash.clear();m_bom=m_crlf=m_unknown=false;m_new=m_hasDocument=true;
    m_status="Nouveau Markdown — choisissez un chemin absolu .md pour enregistrer.";emit changed();emit loaded();
}
void DocumentsController::open(const QString &path){
    if(m_busy || !m_remote->connected())return;
    if(dirty()){m_status="Conservez ou abandonnez le brouillon avant d'ouvrir un autre document.";emit changed();return;}
    const auto clean=cleanAbsolutePath(path);
    if(!clean || !markdownPath(*clean)){m_status="Chemin absolu .md ou .markdown requis.";emit changed();return;}
    const auto request=m_remote->connectionRequest();const int token=m_remote->connectionToken(),seq=++m_seq;
    const auto label=m_remote->serverLabel();m_operationToken=token;m_busy=true;m_writing=false;m_status="Lecture du Markdown…";emit changed();
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,target=*clean,label]{
        const auto result=watcher->result();watcher->deleteLater();if(seq!=m_seq || !m_remote->connected() || token!=m_remote->connectionToken())return;
        m_busy=false;
        const auto decoded=decodeMarkdown(result.data,result.truncated);
        if(result.status!=SshResult::Status::Ok || !decoded.error.isEmpty())m_status=result.status==SshResult::Status::Ok?decoded.error:result.message;
        else {
            m_path=target;m_text=m_base=decoded.text;m_bom=decoded.bom;m_crlf=decoded.crlf;m_hash=QCryptographicHash::hash(result.data,QCryptographicHash::Sha256);
            m_sourceToken=token;m_sourceLabel=label;m_hasDocument=true;m_new=m_unknown=false;m_status="Markdown chargé · UTF-8 · "+QString(m_crlf?"CRLF":"LF");
            emit changed();emit loaded();return;
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
    const auto request=m_remote->connectionRequest();const auto hash=m_hash;const auto text=m_text;
    const int token=m_remote->connectionToken(),seq=++m_seq;m_operationToken=token;m_busy=true;m_writing=true;m_status="Enregistrement sûr en cours…";emit changed();
    auto *watcher=new QFutureWatcher<SshResult>(this);
    connect(watcher,&QFutureWatcher<SshResult>::finished,this,[this,watcher,seq,token,text]{
        const auto result=watcher->result();watcher->deleteLater();if(seq!=m_seq || !m_remote->connected() || token!=m_remote->connectionToken())return;
        m_busy=false;
        if(result.status==SshResult::Status::Ok){m_path=result.directory;m_base=m_text=text;m_hash=result.data;m_new=false;m_status="Enregistré : "+m_path;emit changed();emit saved(m_path);}
        else {m_unknown=result.code=="unknown";m_status=result.code=="conflict"?"Conflit : la version du serveur a changé. Brouillon conservé ; enregistrez une copie.":m_unknown?"Résultat inconnu. Aucun rejeu : vérifiez le fichier serveur. Brouillon conservé.":result.message;emit changed();}
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,target=*clean,bytes,hash,copy]{
        SshSession session;auto r=session.open(request);if(r.status!=SshResult::Status::Ok)return r;
        return copy?session.saveCopy(parentPath(target),target.section('/',-1),bytes):session.saveFile(target,bytes,hash,false);
    }));
}
}
