#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <algorithm>

namespace fortiq {
// Saved Bash history lines only; never infer commands from terminal keystrokes.
inline QVariantList terminalHistoryRows(const QByteArray &data) {
    QVariantList rows;
    for(auto line:QString::fromUtf8(data.left(65536)).split('\n')) {
        if(line.isEmpty())continue;
        if(line.startsWith('#') && line.mid(1).size()>=9
           && std::all_of(line.cbegin()+1,line.cend(),[](QChar c){return c.isDigit();}))continue;
        QString clean;
        for(QChar c:line.left(4096))if(c=='\t' || (c.unicode()>=32 && c.unicode()!=127))clean+=c;
        if(!clean.isEmpty())rows.append(QVariantMap{{"text",clean}});
    }
    while(rows.size()>200)rows.removeFirst();
    std::reverse(rows.begin(),rows.end());return rows;
}
}
