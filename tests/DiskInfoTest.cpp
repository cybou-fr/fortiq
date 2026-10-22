#include <QtTest>
#include "core/DiskInfo.h"
class DiskInfoTest:public QObject {
    Q_OBJECT
private slots:
    void devicesAndUnicode() {
        const auto data=QString::fromUtf8(R"({"blockdevices":[{"name":"sda1","path":"/dev/sda1","type":"part","pkname":"sda","size":"1099511627776","label":"Été <b>Привет</b>","fstype":null,"mountpoint":"/media/été espace","ro":true}]})").toUtf8();
        const auto r=fortiq::parseDiskRows(data,true);QVERIFY(r.error.isEmpty());QCOMPARE(r.rows.size(),1);
        const auto row=r.rows.first().toMap();QCOMPARE(row["size"].toLongLong(),1099511627776LL);
        QCOMPARE(row["label"].toString(),QString::fromUtf8("Été <b>Привет</b>"));QVERIFY(row["readOnly"].toBool());
        QCOMPARE(row["avail"].toLongLong(),-1);QCOMPARE(row["sizeText"].toString(),QString("1.0 Tio"));
    }
    void mountsAndUnknown() {
        const auto r=fortiq::parseDiskRows(R"({"filesystems":[{"source":"/dev/sda1","target":"/home/été espace","fstype":"ext4","size":1000,"used":"500","avail":400},{"source":"tmpfs","target":"/run","size":null,"used":null}]})",false);
        QVERIFY(r.error.isEmpty());QCOMPARE(r.rows.size(),2);QCOMPARE(r.rows[0].toMap()["fraction"].toDouble(),0.5);
        QCOMPARE(r.rows[1].toMap()["size"].toLongLong(),-1);QVERIFY(!r.rows[1].toMap()["readOnlyKnown"].toBool());
        QVERIFY(fortiq::parseDiskRows("{\"filesystems\":[]}",false).error.isEmpty());
    }
    void malformedAndLimits() {
        for(const QByteArray &data:QList<QByteArray>{"{}","truncated",R"({"filesystems":[5]})",R"({"filesystems":[{"source":"x","target":"relative"}]})",R"({"filesystems":[{"source":"x","target":"/","size":-1}]})",R"({"filesystems":[{"source":"x","target":"/","size":"oops"}]})",QByteArray(1048577,'x')})
            QVERIFY(!fortiq::parseDiskRows(data,false).error.isEmpty());
        QJsonArray rows;for(int i=0;i<513;++i)rows.append(QJsonObject{{"source","x"},{"target","/"}});
        QVERIFY(!fortiq::parseDiskRows(QJsonDocument(QJsonObject{{"filesystems",rows}}).toJson(),false).error.isEmpty());
        const auto exact=fortiq::parseDiskRows(R"({"filesystems":[{"source":"x","target":"/","size":9007199254740993}]})",false);
        QVERIFY(exact.error.isEmpty());QCOMPARE(exact.rows[0].toMap()["size"].toLongLong(),9007199254740993LL);
    }
};
QTEST_GUILESS_MAIN(DiskInfoTest)
#include "DiskInfoTest.moc"
