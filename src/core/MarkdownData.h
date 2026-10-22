#pragma once
#include <QStringDecoder>
#include <QByteArray>
namespace fortiq {
inline constexpr int MarkdownLimit=262144;
struct MarkdownData { QString text,error;bool bom=false,crlf=false; };
inline MarkdownData decodeMarkdown(const QByteArray &bytes,bool truncated=false) {
    MarkdownData out;
    if(truncated || bytes.size()>MarkdownLimit){out.error="Document incomplet ou supérieur à 256 Kio.";return out;}
    if(bytes.contains('\0')){out.error="Document binaire non modifiable.";return out;}
    out.bom=bytes.startsWith("\xEF\xBB\xBF");
    QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);out.text=decoder.decode(out.bom?bytes.mid(3):bytes);
    if(decoder.hasError()){out.error="Seul UTF-8 est pris en charge.";return out;}
    const int pairs=out.text.count("\r\n");
    if(out.text.count('\r')!=pairs || (pairs && out.text.count('\n')!=pairs)){
        out.error="Fins de ligne mixtes ou CR seules non prises en charge.";return out;
    }
    out.crlf=pairs>0;out.text.replace("\r\n","\n");return out;
}
inline QByteArray encodeMarkdown(QString text,bool bom,bool crlf) {
    text.replace("\r\n","\n");if(crlf)text.replace("\n","\r\n");
    auto bytes=text.toUtf8();if(bom)bytes.prepend("\xEF\xBB\xBF");return bytes;
}
inline bool markdownPath(const QString &path){return path.endsWith(".md",Qt::CaseInsensitive)||path.endsWith(".markdown",Qt::CaseInsensitive);}
}
