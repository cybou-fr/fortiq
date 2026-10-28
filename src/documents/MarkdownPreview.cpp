#include "MarkdownPreview.h"
#include <QPainter>
#include <QBuffer>
#include <QImageReader>
#include <QAbstractTextDocumentLayout>
MarkdownPreview::MarkdownPreview(QQuickItem *parent):QQuickPaintedItem(parent){
    QFont font;font.setPixelSize(15);m_doc.setDefaultFont(font);m_doc.setDocumentMargin(12);
    m_doc.setDefaultStyleSheet("body { color: #e4ecf8; } a { color: #8dbeff; } code { color: #b7d6ff; }");
    connect(this,&MarkdownPreview::scrollYChanged,this,[this]{update();});
}
void MarkdownPreview::setSource(const QString &source){
    // The preview intentionally renders only the first 64k characters.
    // Editing beyond that boundary must not rebuild the QTextDocument.
    constexpr qsizetype kPreviewLimit = 64000;
    const bool wasLimited=m_source.size()>kPreviewLimit;
    const bool nowLimited=source.size()>kPreviewLimit;
    const QString visibleSource=source.left(kPreviewLimit);
    if(visibleSource==m_source.left(kPreviewLimit)){
        if(wasLimited!=nowLimited){m_source=source;emit changed();}
        return;
    }
    m_source=source;
    m_doc.setMarkdown(visibleSource,QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
    m_doc.setTextWidth(qMax(40.0,width()));
    update();emit changed();
}
void MarkdownPreview::geometryChange(const QRectF &now,const QRectF &old){
    QQuickPaintedItem::geometryChange(now,old);if(now.width()!=old.width()){m_doc.setTextWidth(qMax(40.0,now.width()));update();emit changed();}
}
void MarkdownPreview::paint(QPainter *p){
    p->save();p->setClipRect(boundingRect());p->translate(0,-m_scrollY);
    QAbstractTextDocumentLayout::PaintContext context;context.palette.setColor(QPalette::Text,QColor("#e4ecf8"));context.clip=QRectF(0,m_scrollY,width(),height());
    m_doc.documentLayout()->draw(p,context);p->restore();
}

void MarkdownPreview::clearImages(){
    m_doc.images.clear();
    m_doc.setMarkdown(m_source.left(64000),QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
    m_doc.setTextWidth(qMax(40.0,width()));update();emit changed();
}
void MarkdownPreview::setImageData(const QString &relativeUrl,const QString &base64){
    if(relativeUrl.isEmpty()||base64.size()>2800000)return;
    const QByteArray bytes=QByteArray::fromBase64(base64.toLatin1());
    if(bytes.isEmpty()||bytes.size()>2097152)return;
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(false);
    const QSize dimensions=reader.size();
    if(!dimensions.isValid()||dimensions.width()>4096||dimensions.height()>4096||
       qint64(dimensions.width())*dimensions.height()>8000000)return;
    const QImage image=reader.read();
    if(image.isNull())return;
    m_doc.images.insert(relativeUrl,image);
    m_doc.setMarkdown(m_source.left(64000),QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
    m_doc.setTextWidth(qMax(40.0,width()));update();emit changed();
}
