#include "MarkdownPreview.h"
#include <QPainter>
#include <QAbstractTextDocumentLayout>
MarkdownPreview::MarkdownPreview(QQuickItem *parent):QQuickPaintedItem(parent){
    QFont font;font.setPixelSize(15);m_doc.setDefaultFont(font);m_doc.setDocumentMargin(12);
    m_doc.setDefaultStyleSheet("body { color: #e4ecf8; } a { color: #8dbeff; } code { color: #b7d6ff; }");
    connect(this,&MarkdownPreview::scrollYChanged,this,[this]{update();});
}
void MarkdownPreview::setSource(const QString &source){
    if(source==m_source)return;m_source=source;
    m_doc.setMarkdown(source.left(64000),QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
    m_doc.setTextWidth(qMax(40.0,width()));update();emit changed();
}
void MarkdownPreview::geometryChange(const QRectF &now,const QRectF &old){
    QQuickPaintedItem::geometryChange(now,old);if(now.width()!=old.width()){m_doc.setTextWidth(qMax(40.0,now.width()));update();emit changed();}
}
void MarkdownPreview::paint(QPainter *p){
    p->save();p->setClipRect(boundingRect());p->translate(0,-m_scrollY);
    QAbstractTextDocumentLayout::PaintContext context;context.palette.setColor(QPalette::Text,QColor("#e4ecf8"));context.clip=QRectF(0,m_scrollY,width(),height());
    m_doc.documentLayout()->draw(p,context);p->restore();
}
