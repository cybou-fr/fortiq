#pragma once
#include <QObject>
#include <QThreadPool>
#include <QByteArray>
#include <QTimer>
#include <QVariantMap>
namespace fortiq {
class RemoteController;
class DocumentsController:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    Q_PROPERTY(QString sourceServerId READ sourceServerId NOTIFY changed)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY changed)
    Q_PROPERTY(bool isNew READ isNew NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool sourceAvailable READ sourceAvailable NOTIFY changed)
    // Server-side drafts (docs/MDOK.md): a draft of THIS document found on its server after opening (savedAt, baseMatches, lines), and the
    // state of the last autosave. Conflict screen: shown when the server version changed under a save.
    Q_PROPERTY(QVariantMap recovery READ recovery NOTIFY changed)
    Q_PROPERTY(QString draftStatus READ draftStatus NOTIFY changed)
    Q_PROPERTY(bool conflictActive READ conflictActive NOTIFY changed)
    Q_PROPERTY(bool conflictBusy READ conflictBusy NOTIFY changed)
    Q_PROPERTY(QString conflictDiff READ conflictDiff NOTIFY changed)
    Q_PROPERTY(QString conflictSummary READ conflictSummary NOTIFY changed)
    Q_PROPERTY(bool pdfMode READ pdfMode NOTIFY changed)
    Q_PROPERTY(int pdfPage READ pdfPage NOTIFY changed)
    Q_PROPERTY(int pdfPages READ pdfPages NOTIFY changed)
    Q_PROPERTY(QString pdfImage READ pdfImage NOTIFY changed)
public:
    explicit DocumentsController(RemoteController *remote,QObject *parent=nullptr);
    ~DocumentsController() override;
    QString path()const{return m_path;} QString text()const{return m_text;}
    QString sourceServerId()const{return m_sourceServerId;}
    Q_INVOKABLE bool isCurrentDocument(const QString &path) const;
    QString sourceLabel()const{return m_sourceLabel;} QString status()const{return m_status;}
    bool busy()const{return m_busy;} bool dirty()const{return m_text!=m_base;}
    bool hasDocument()const{return m_hasDocument;} bool isNew()const{return m_new;}
    bool canSave()const;
    bool sourceAvailable()const;
    bool pdfMode()const{return m_pdfMode;}
    int pdfPage()const{return m_pdfPage;}
    int pdfPages()const{return m_pdfPages;}
    QString pdfImage()const{return m_pdfImage;}
    Q_INVOKABLE void openPdf(const QString &path);
    Q_INVOKABLE void showPdfPage(int page);
    Q_INVOKABLE void exportPdf(const QString &destination);
    QVariantMap recovery()const{return m_recovery;} QString draftStatus()const{return m_draftStatus;}
    bool conflictActive()const{return m_conflict;} bool conflictBusy()const{return m_conflictBusy;}
    QString conflictDiff()const{return m_conflictDiff;} QString conflictSummary()const{return m_conflictSummary;}
    void setText(const QString &text);
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void createNew();
    Q_INVOKABLE void discardDraft();
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs(const QString &path);
    Q_INVOKABLE void restoreDraft();                        // put the server draft into the editor (a stale base turns the next save into a conflict)
    Q_INVOKABLE void dismissRecovery(bool deleteDraft);
    Q_INVOKABLE void resolveConflictSaveCopy();             // my version -> a new file next to the document ("name (ma version).md")
    Q_INVOKABLE void resolveConflictTakeServer();           // load the server version; my text stays in the server draft
    Q_INVOKABLE void closeConflict();                       // keep my text, decide later
    Q_INVOKABLE void setAutosaveDelay(int ms);              // tests; default 5000 ms after the last keystroke
signals:
    void changed();
    void loaded();
    void saved(const QString &path);
private:
    void write(const QString &path,bool copy);
    void scheduleDraft();
    void writeDraftNow();
    void deleteDraft();
    void deleteDraftFor(const QString &path);
    void checkDraft();
    void beginConflict();
    void renderPdfPage(const QString &path, int page, int pages);
    RemoteController *m_remote;
    QThreadPool m_pool;
    int m_seq=0,m_sourceToken=-1,m_operationToken=-1;
    bool m_busy=false,m_writing=false,m_hasDocument=false,m_new=false,m_bom=false,m_crlf=false,m_unknown=false;
    QString m_path,m_text,m_base,m_sourceServerId,m_sourceLabel,m_status="Ouvrez un document Markdown du serveur.";
    QByteArray m_hash;
    bool m_pdfMode=false;
    int m_pdfPage=0,m_pdfPages=0;
    QString m_pdfImage;
    QTimer m_draftTimer;
    int m_autosaveMs=5000;
    bool m_draftUploadPending=false; // keep at most one in-flight draft upload
    QString m_pendingSavePath; // one explicit user intent, consumed after draft upload
    bool m_pendingSaveCopy=false;
    int m_pendingSaveEpoch=-1;
    QVariantMap m_recovery;QString m_recoveryText,m_recoveryBase,m_draftStatus;
    bool m_conflict=false,m_conflictBusy=false;QString m_conflictDiff,m_conflictSummary,m_conflictServerText;QByteArray m_conflictServerHash;bool m_conflictBom=false,m_conflictCrlf=false;
};
}
