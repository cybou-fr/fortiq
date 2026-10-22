#pragma once
#include <QObject>
#include <QThreadPool>
#include <QByteArray>
namespace fortiq {
class RemoteController;
class DocumentsController:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY changed)
    Q_PROPERTY(bool isNew READ isNew NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
public:
    explicit DocumentsController(RemoteController *remote,QObject *parent=nullptr);
    ~DocumentsController() override;
    QString path()const{return m_path;} QString text()const{return m_text;}
    QString sourceLabel()const{return m_sourceLabel;} QString status()const{return m_status;}
    bool busy()const{return m_busy;} bool dirty()const{return m_text!=m_base;}
    bool hasDocument()const{return m_hasDocument;} bool isNew()const{return m_new;}
    bool canSave()const;
    void setText(const QString &text);
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void createNew();
    Q_INVOKABLE void discardDraft();
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs(const QString &path);
signals:
    void changed();
    void loaded();
    void saved(const QString &path);
private:
    void write(const QString &path,bool copy);
    RemoteController *m_remote;
    QThreadPool m_pool;
    int m_seq=0,m_sourceToken=-1,m_operationToken=-1;
    bool m_busy=false,m_writing=false,m_hasDocument=false,m_new=false,m_bom=false,m_crlf=false,m_unknown=false;
    QString m_path,m_text,m_base,m_sourceLabel,m_status="Ouvrez un document Markdown du serveur.";
    QByteArray m_hash;
};
}
