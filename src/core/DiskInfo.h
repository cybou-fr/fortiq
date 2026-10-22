#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <cmath>

namespace fortiq {
struct DiskRows { QVariantList rows; QString error; };
inline QString diskBytes(qint64 n) {
    if(n<0)return QStringLiteral("Indisponible");
    const QStringList units{"o","Kio","Mio","Gio","Tio","Pio"};
    double value=n;int unit=0;while(value>=1024 && unit<units.size()-1){value/=1024;++unit;}
    return QString::number(value,'f',unit?1:0)+" "+units[unit];
}
inline DiskRows parseDiskRows(const QByteArray &data,bool devices) {
    DiskRows out;
    auto fail=[&](const QString &why){out.rows.clear();out.error=why;return out;};
    if(data.size()>1048576)return fail(QStringLiteral("Réponse trop volumineuse (1 Mio)."));
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(data,&error);
    const QString key=devices?QStringLiteral("blockdevices"):QStringLiteral("filesystems");
    if(error.error!=QJsonParseError::NoError || !doc.isObject() || !doc.object().value(key).isArray())
        return fail(QStringLiteral("Réponse JSON invalide ou contrat non pris en charge."));
    const auto rows=doc.object().value(key).toArray();
    if(rows.size()>512)return fail(QStringLiteral("Limite de 512 entrées dépassée."));
    for(const auto &value:rows) {
        if(!value.isObject())return fail(QStringLiteral("Entrée JSON invalide."));
        const auto row=value.toObject();QVariantMap item;
        for(const QString &field:QStringList{"name","path","type","pkname","fstype","label","mountpoint","source","target","options"}) {
            const auto v=row.value(field);
            if(!v.isUndefined() && !v.isNull() && (!v.isString() || v.toString().size()>4096 || v.toString().contains(QChar::Null)))
                return fail(QStringLiteral("Champ texte invalide."));
            item[field]=v.toString();
        }
        if(devices ? (item["name"].toString().isEmpty() || !item["path"].toString().startsWith('/') || item["type"].toString().isEmpty())
                   : (!item["target"].toString().startsWith('/') || item["source"].toString().isEmpty()))
            return fail(QStringLiteral("Identité de périphérique ou montage invalide."));
        for(const QString &field:QStringList{"size","used","avail"}) {
            const auto v=row.value(field);qint64 number=-1;bool valid=true;
            if(v.isString())number=v.toString().toLongLong(&valid);
            else if(v.isDouble()) { number=v.toInteger(-1);valid=number>=0 && double(number)==v.toDouble(); }
            else valid=v.isNull() || v.isUndefined();
            if(!valid || (number<0 && !v.isNull() && !v.isUndefined()))return fail(QStringLiteral("Taille en octets invalide."));
            item[field]=number;item[field+"Text"]=diskBytes(number);
        }
        const auto ro=row.value("ro");
        item["readOnlyKnown"]=ro.isBool() || ro==QJsonValue(0) || ro==QJsonValue(1) || ro==QJsonValue("0") || ro==QJsonValue("1");
        item["readOnly"]=ro.isBool()?ro.toBool():(ro.toVariant().toString()=="1");
        const auto used=item["used"].toLongLong(),size=item["size"].toLongLong();
        item["fraction"]=used>=0 && size>0?qBound(0.0,double(used)/double(size),1.0):-1.0;
        out.rows+=item;
    }
    return out;
}
inline QByteArray diskDevicesScript() { return "export LC_ALL=C\nlsblk --json --list --bytes --output NAME,PATH,TYPE,PKNAME,SIZE,FSTYPE,LABEL,MOUNTPOINT,RO\n"; }
inline QByteArray diskMountsScript() { return "export LC_ALL=C\nfindmnt --json --list --df --bytes --output SOURCE,TARGET,FSTYPE,SIZE,USED,AVAIL,OPTIONS\n"; }
}
