#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
#include <memory>

namespace fortiq {
class RemoteController;
class DisksController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString versions READ versions NOTIFY changed)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)
    Q_PROPERTY(QString deviceError READ deviceError NOTIFY changed)
    Q_PROPERTY(QString mountError READ mountError NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList mounts READ mounts NOTIFY changed)
public:
    explicit DisksController(RemoteController *remote,QObject *parent=nullptr);
    ~DisksController() override;
    bool busy()const{return m_busy;}
    QString state()const{return m_state;} QString status()const{return m_status;}
    QString versions()const{return m_versions;} QString updated()const{return m_updated;}
    QString deviceError()const{return m_deviceError;} QString mountError()const{return m_mountError;}
    QVariantList devices()const{return m_devices;} QVariantList mounts()const{return m_mounts;}
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setWindowActive(bool active);
signals:
    void changed();
private:
    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    int m_seq=0,m_token=-1;
    bool m_busy=false,m_active=false;
    QString m_state="disconnected",m_status="Aucun serveur connecté",m_versions,m_updated,m_deviceError,m_mountError;
    QVariantList m_devices,m_mounts;
};
}
