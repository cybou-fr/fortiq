#include "DisksController.h"
#include "DiskInfo.h"
#include "RemoteController.h"
#include "TaskManager.h"
#include "../ssh/SshClient.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QDateTime>

namespace fortiq {
namespace {
struct Snapshot { DiskRows devices,mounts;QString versions,error; };
DiskRows readRows(SshSession &session,const QByteArray &script,bool devices,const std::shared_ptr<std::atomic_bool> &cancel) {
    const auto result=session.runScript(script,1048576,[cancel]{return !cancel->load();});
    if(result.status!=SshResult::Status::Ok || result.code!="exit:0" || result.truncated)
        return {{},result.truncated?QStringLiteral("Réponse incomplète : limite 1 Mio."):
            QStringLiteral("%1 indisponible (droits, option, outil absent ou SSH) : %2 %3").arg(devices?"lsblk":"findmnt",result.code,result.message)};
    return parseDiskRows(result.data,devices);
}
}
DisksController::DisksController(RemoteController *remote,QObject *parent):QObject(parent),m_remote(remote) {
    m_pool.setMaxThreadCount(1);
    if(remote->tasks())connect(remote->tasks(),&TaskManager::cancelRequested,this,[this](int id){if(id!=0 && id==m_task && m_cancel)m_cancel->store(true);});
    connect(remote,&RemoteController::changed,this,[this]{
        if(m_remote->connected() && m_remote->connectionToken()==m_token)return;
        if(m_cancel)m_cancel->store(true);
        ++m_seq;m_busy=false;m_token=m_remote->connected()?m_remote->connectionToken():-1;
        m_devices.clear();m_mounts.clear();m_versions.clear();m_updated.clear();m_deviceError.clear();m_mountError.clear();
        m_state="disconnected";m_status=QStringLiteral("Source déconnectée ou changée.");emit changed();
        if(m_active && m_remote->connected())reload();
    });
}
DisksController::~DisksController(){if(m_cancel)m_cancel->store(true);m_pool.waitForDone();}
void DisksController::setWindowActive(bool active){const bool opening=active&&!m_active;m_active=active;if(opening && m_remote->connected())reload();}
void DisksController::reload() {
    if(m_busy || !m_remote->connected())return;
    m_token=m_remote->connectionToken();const int token=m_token,seq=++m_seq;
    const auto request=m_remote->connectionRequest();m_cancel=std::make_shared<std::atomic_bool>(false);auto cancel=m_cancel;
    m_busy=true;m_state="loading";m_status=QStringLiteral("Lecture des disques et systèmes de fichiers…");emit changed();
    int taskId=0;
    if(TaskManager *tasks=m_remote->tasks()){
        TaskManager::Spec spec;spec.kind=QStringLiteral("disksRead");spec.title=QStringLiteral("Lecture des disques");spec.source=m_remote->serverLabel();
        spec.generation=token;spec.cancellable=true;spec.guards=false;spec.session=true;spec.transient=true;
        taskId=m_task=tasks->begin(spec);
    }
    auto *watcher=new QFutureWatcher<Snapshot>(this);
    connect(watcher,&QFutureWatcher<Snapshot>::finished,this,[this,watcher,seq,token,taskId,cancel]{
        const auto result=watcher->result();watcher->deleteLater();
        TaskManager *tasks=m_remote->tasks();
        if(seq!=m_seq || !m_remote->connected() || token!=m_remote->connectionToken()){if(tasks && taskId)tasks->markCancelled(taskId,QStringLiteral("Session changée."));return;}
        m_busy=false;
        if(tasks && taskId){
            if(cancel->load())tasks->markCancelled(taskId,QStringLiteral("Annulé."));
            else if(!result.error.isEmpty() || (!result.devices.error.isEmpty() && !result.mounts.error.isEmpty()))tasks->fail(taskId,!result.error.isEmpty()?result.error:result.devices.error);
            else tasks->succeed(taskId,QString());
        }m_devices=result.devices.rows;m_mounts=result.mounts.rows;m_versions=result.versions;
        m_deviceError=result.devices.error;m_mountError=result.mounts.error;
        if(!result.error.isEmpty()){m_deviceError=m_mountError=result.error;}
        const int errors=int(!m_deviceError.isEmpty())+int(!m_mountError.isEmpty());
        m_state=errors==2?"error":errors==1?"partial":"ready";
        m_status=errors==2?QStringLiteral("Sources de stockage indisponibles."):errors==1?QStringLiteral("Vue partielle — une source indisponible."):QStringLiteral("Lecture seule · capacité du serveur sélectionné");
        m_updated=errors==2?QString():QDateTime::currentDateTime().toString("HH:mm:ss");emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool,[request,cancel]{
        Snapshot snapshot;SshSession session;const auto opened=session.open(request);
        if(opened.status!=SshResult::Status::Ok){snapshot.error=opened.message;return snapshot;}
        if(cancel->load())return snapshot;
        const auto versions=session.runScript("export LC_ALL=C\nlsblk --version\nfindmnt --version\n",4096,[cancel]{return !cancel->load();});
        if(versions.status==SshResult::Status::Ok && !versions.truncated)snapshot.versions=QString::fromUtf8(versions.data).trimmed();
        snapshot.devices=readRows(session,diskDevicesScript(),true,cancel);
        if(!cancel->load())snapshot.mounts=readRows(session,diskMountsScript(),false,cancel);
        return snapshot;
    }));
}
}
