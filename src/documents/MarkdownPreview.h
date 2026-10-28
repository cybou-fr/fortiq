#pragma once
#include <QQuickPaintedItem>
#include <QTextDocument>
#include <QHash>
#include <QImage>
#include <QtQml/qqmlregistration.h>
class SafeMarkdownDocument:public QTextDocument {
public:
    QVariant loadResource(int type,const QUrl &url)override {
        if(type==QTextDocument::ImageResource)return images.value(url.toString());
        return {};
    }
    QHash<QString,QImage> images;
};
class MarkdownPreview:public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(qreal scrollY MEMBER m_scrollY NOTIFY scrollYChanged)
    Q_PROPERTY(qreal documentHeight READ documentHeight NOTIFY changed)
    Q_PROPERTY(bool limited READ limited NOTIFY changed)
public:
    explicit MarkdownPreview(QQuickItem *parent=nullptr);
    QString source()const{return m_source;}void setSource(const QString &source);
    qreal documentHeight()const{return m_doc.size().height()+24;}
    bool limited()const{return m_source.size()>64000;}
    Q_INVOKABLE QString plainText()const{return m_doc.toPlainText();}
    Q_INVOKABLE void clearImages();
    Q_INVOKABLE void setImageData(const QString &relativeUrl,const QString &base64);
    void paint(QPainter *painter)override;
signals:
    void changed();void scrollYChanged();
protected:
    void geometryChange(const QRectF &now,const QRectF &old)override;
private:
    SafeMarkdownDocument m_doc;QString m_source;qreal m_scrollY=0;
};
