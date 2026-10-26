#include "core/SecureWipe.h"
#include "db/DbInfo.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSet>
#include <QtTest>

using namespace fortiq;

class DbTest : public QObject {
    Q_OBJECT
private slots:
    void profileValidation() {
        DbProfile p;
        p.name = "Base"; p.engine = DbEngine::Sqlite; p.path = "/home/u/a.db";
        QVERIFY(validateProfile(p).isEmpty());
        p.path = "relative.db";
        QVERIFY(!validateProfile(p).isEmpty());
        p.path = "/a\nb";
        QVERIFY(!validateProfile(p).isEmpty());
        p.name = "";
        p.path = "/a.db";
        QVERIFY(!validateProfile(p).isEmpty());

        DbProfile g;
        g.name = "PG"; g.engine = DbEngine::Postgres; g.host = ""; g.port = 5432; g.user = "fqdb"; g.database = "fqtest";
        QVERIFY(validateProfile(g).isEmpty());
        g.host = "db.example.org";
        QVERIFY(validateProfile(g).isEmpty());
        g.host = "x;rm -rf";
        QVERIFY(!validateProfile(g).isEmpty());
        g.host = "";
        g.port = 70000;
        QVERIFY(!validateProfile(g).isEmpty());
        g.port = 5432; g.user = "a b";
        QVERIFY(!validateProfile(g).isEmpty());
        g.user = "u"; g.database = "d'; drop";
        QVERIFY(!validateProfile(g).isEmpty());
    }

    void profileRoundTrip() {
        DbProfile g;
        g.id = "i1"; g.serverId = "s1"; g.name = "PG"; g.engine = DbEngine::Postgres; g.host = "h"; g.port = 5433; g.user = "u"; g.database = "d"; g.hasPassword = true;
        const DbProfile r = profileFromVariant(profileToVariant(g));
        QCOMPARE(r.id, g.id); QCOMPARE(r.serverId, g.serverId); QCOMPARE(r.port, 5433); QVERIFY(r.engine == DbEngine::Postgres); QVERIFY(r.hasPassword);
        QVERIFY(!profileToVariant(g).contains("password"));
    }

    void scanBasics() {
        QCOMPARE(scanSql("select 1", DbEngine::Postgres).statements, 1);
        QCOMPARE(scanSql("select 1;", DbEngine::Postgres).statements, 1);
        QCOMPARE(scanSql("select 1; select 2", DbEngine::Postgres).statements, 2);
        QCOMPARE(scanSql("select ';' ; ", DbEngine::Postgres).statements, 1);
        QCOMPARE(scanSql("-- c\nselect 1 /* ; */", DbEngine::Postgres).statements, 1);
        QCOMPARE(scanSql("  \n ; ; ", DbEngine::Postgres).statements, 0);
        QCOMPARE(scanSql("WITH x AS (select 1) select * from x", DbEngine::Postgres).firstKeyword, QString("with"));
        QCOMPARE(scanSql("(select 1) union (select 2)", DbEngine::Postgres).firstKeyword, QString("select"));
    }

    void scanQuoting() {
        QVERIFY(scanSql("select 'it''s ; x'", DbEngine::Postgres).statements == 1);
        QVERIFY(scanSql("select E'a\\'; b'", DbEngine::Postgres).statements == 1);
        QCOMPARE(scanSql("select 'a\\'; select 2", DbEngine::Postgres).statements, 2);   // standard string: the backslash is a literal, the quote closes it
        QVERIFY(scanSql("select $$a;\\b$$", DbEngine::Postgres).statements == 1);
        QVERIFY(!scanSql("select $$a;\\b$$", DbEngine::Postgres).metaCommand);
        QVERIFY(scanSql("select $t$ $$ ; $t$", DbEngine::Postgres).statements == 1);
        QVERIFY(scanSql("select \"a;b\" from t", DbEngine::Postgres).statements == 1);
        QVERIFY(scanSql("select /* a /* nested ; */ ; */ 1", DbEngine::Postgres).statements == 1);
        QVERIFY(scanSql("select 'open", DbEngine::Postgres).unterminated);
        QVERIFY(scanSql("select /* open", DbEngine::Postgres).unterminated);
        QVERIFY(scanSql("select $x$ open", DbEngine::Postgres).unterminated);
        QVERIFY(!scanSql("select 1 -- trailing", DbEngine::Postgres).unterminated);
        QVERIFY(scanSql("select [a;b] from t", DbEngine::Sqlite).statements == 1);
        QVERIFY(scanSql("select `a;b` from t", DbEngine::Sqlite).statements == 1);
        QVERIFY(scanSql("select $1", DbEngine::Sqlite).statements == 1);
    }

    void scanMetaCommands() {
        QVERIFY(scanSql("\\! id", DbEngine::Postgres).metaCommand);
        QVERIFY(scanSql("select 1\n\\g", DbEngine::Postgres).metaCommand);
        QVERIFY(scanSql("select 1 \\gexec", DbEngine::Postgres).metaCommand);
        QVERIFY(!scanSql("select '\\!'", DbEngine::Postgres).metaCommand);
        QVERIFY(!scanSql("select 1 -- \\!", DbEngine::Postgres).metaCommand);
        QVERIFY(scanSql(".shell id", DbEngine::Sqlite).metaCommand);
        QVERIFY(scanSql("select 1;\n.shell id", DbEngine::Sqlite).metaCommand);
        QVERIFY(scanSql("select 1;\n  .read x", DbEngine::Sqlite).metaCommand);
        QVERIFY(!scanSql("select 1.5, 'a\n.b'", DbEngine::Sqlite).metaCommand);
        QVERIFY(!scanSql("select 1\n-- .shell", DbEngine::Sqlite).metaCommand);
    }

    void refusals() {
        const auto pg = DbEngine::Postgres, sq = DbEngine::Sqlite;
        QVERIFY(readOnlyRefusal("select 1", pg).isEmpty());
        QVERIFY(readOnlyRefusal("  -- hello\n  WITH a AS (select 1) select * from a;", pg).isEmpty());
        QVERIFY(readOnlyRefusal("explain select 1", pg).isEmpty());
        QVERIFY(readOnlyRefusal("show server_version", pg).isEmpty());
        QVERIFY(readOnlyRefusal("table people", pg).isEmpty());
        QVERIFY(readOnlyRefusal("values (1),(2)", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("insert into t values (1)", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("drop table t", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("set default_transaction_read_only = off", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("begin", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("copy t to program 'id'", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("select 1; drop table t", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("\\! id", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("select 'x", pg).isEmpty());
        QVERIFY(readOnlyRefusal("pragma table_info(t)", sq).isEmpty());
        QVERIFY(!readOnlyRefusal("pragma table_info(t)", pg).isEmpty());
        QVERIFY(!readOnlyRefusal("delete from t", sq).isEmpty());
        QVERIFY(!readOnlyRefusal("attach database 'x' as y", sq).isEmpty());
        QVERIFY(!readOnlyRefusal(".shell id", sq).isEmpty());
        QVERIFY(!readOnlyRefusal(QString("select '") + QString(120 * 1024, 'a') + "'", pg).isEmpty());
        QVERIFY(!readOnlyRefusal(QString("select 1") + QChar::Null, pg).isEmpty());
    }

    // Audit 23.10.2026: the sqlite3 shell provides writefile()/readfile()/edit()/load_extension() as SQL functions. They passed the keyword check and
    // "-readonly" does not stop them (live proof: writefile created a file). They are refused here and `-safe` removes them in the shell.
    void shellFunctionsAreRefused() {
        const auto sq = DbEngine::Sqlite, pg = DbEngine::Postgres;
        for (const char *sql : { "select writefile('/tmp/x', 'y')", "SELECT WriteFile('/tmp/x','y')", "select readfile('/etc/passwd')", "select edit('x', 'true')",
                                 "select load_extension('/lib/x.so')", "select name, data from fsdir('/etc')", "select 1 where writefile ('/tmp/x','y')",
                                 "with c as (select readfile('/etc/hostname')) select * from c", "select/**/writefile/**/('/tmp/x','y')" }) {
            QVERIFY2(!readOnlyRefusal(QString::fromUtf8(sql), sq).isEmpty(), sql);
            QVERIFY2(readOnlyRefusal(QString::fromUtf8(sql), sq).contains("refus"), sql);
        }
        // Names inside strings, comments and quoted identifiers are data, not calls.
        QVERIFY(readOnlyRefusal("select 'writefile' as a, \"readfile\" from t", sq).isEmpty());
        QVERIFY(readOnlyRefusal("select 1 -- readfile(\n", sq).isEmpty());
        QVERIFY(readOnlyRefusal("select 1 /* edit */", sq).isEmpty());
        QVERIFY(readOnlyRefusal("select my_readfile_count from t", sq).isEmpty());   // only whole identifiers
        for (const char *sql : { "select pg_terminate_backend(123)", "select PG_CANCEL_BACKEND(1)", "select pg_read_file('/etc/passwd')", "select lo_import('/etc/passwd')",
                                 "select pg_reload_conf()", "select set_config('default_transaction_read_only','off',false)", "select * from dblink('x','select 1') as t(a int)",
                                 "select pg_ls_dir('.')", "select pg_switch_wal()" })
            QVERIFY2(!readOnlyRefusal(QString::fromUtf8(sql), pg).isEmpty(), sql);
        QVERIFY(readOnlyRefusal("select pg_sleep(1), now(), version()", pg).isEmpty());
        QVERIFY(readOnlyRefusal("select count(*) from pg_stat_activity", pg).isEmpty());
        // The same names are ordinary words for the other engine's rules.
        QVERIFY(readOnlyRefusal("select writefile from t", pg).isEmpty());
        QCOMPARE(scanSql("select a, b2 from t where c = 'x y' -- z", sq).words, QStringList({ "select", "a", "b2", "from", "t", "where", "c" }));
    }

    // Audit 23.10.2026: random input must never crash the parsers, and a statement the scanner lets through must satisfy the read-only contract.
    void parsersSurviveRandomOutput() {
        QRandomGenerator rng(20261023);
        const QByteArray alphabet = "ab1 ,,\n\n\"\"''XNULL<<N>>x';\r\\\xC3\xA9";
        for (int round = 0; round < 4000; ++round) {
            QByteArray data;
            const int len = rng.bounded(0, 260);
            for (int i = 0; i < len; ++i) data += alphabet.at(rng.bounded(alphabet.size()));
            const int maxRows = rng.bounded(1, 6);
            for (const DbResult &r : { parsePostgresCsv(data, "<<N>>", maxRows, round % 2 == 0), parseSqliteQuote(data, maxRows, round % 2 == 1) }) {
                QVERIFY(r.rows.size() <= maxRows);
                for (const QVariantList &row : r.rows) QCOMPARE(row.size(), r.columns.size());
                QVERIFY(r.numeric.size() == r.columns.size() || r.columns.isEmpty());
            }
        }
    }

    void scannerAcceptsOnlyOneAllowedStatement() {
        QRandomGenerator rng(7);
        const QStringList tokens { "select", "with", "values", "table", "show", "explain", "pragma", "insert", "delete", "drop", "update", "create", "set", "begin", "copy", "do", "call",
                                   "1", "x", "*", "from", "t", "(", ")", ";", ";", "'a'", "'it''s'", "\"q\"", "$$", "$t$", "/*", "*/", "--", "\n", " ", "\\", ".", "[", "]", "`", "E'", "'", "writefile", "pg_read_file" };
        for (const DbEngine engine : { DbEngine::Postgres, DbEngine::Sqlite }) {
            int accepted = 0;
            for (int round = 0; round < 20000; ++round) {
                QString sql;
                const int n = rng.bounded(1, 14);
                for (int i = 0; i < n; ++i) sql += tokens.at(rng.bounded(tokens.size())) + (rng.bounded(4) ? QStringLiteral(" ") : QString());
                if (!readOnlyRefusal(sql, engine).isEmpty()) continue;
                ++accepted;
                const SqlScan s = scanSql(sql, engine);
                QCOMPARE(s.statements, 1);
                QVERIFY2(!s.metaCommand && !s.unterminated, qPrintable(sql));
                static const QSet<QString> pgOk { "select", "with", "values", "table", "show", "explain" }, sqOk { "select", "with", "values", "explain", "pragma" };
                QVERIFY2((engine == DbEngine::Postgres ? pgOk : sqOk).contains(s.firstKeyword), qPrintable(sql));
                QVERIFY2(engine == DbEngine::Sqlite ? !s.words.contains("writefile") : !s.words.contains("pg_read_file"), qPrintable(sql));
            }
            QVERIFY2(accepted > 50, "the generator must produce acceptable statements too");
        }
    }

    // ---- the connected role (audit R4) ----
    static QByteArray roleRow(const QString &super, const QString &createrole, const QString &createdb, const QString &repl, const QString &bypass,
                              const QString &risky, int writable, int total, int schemas, const QString &createDb) {
        return ("rolsuper,rolcreaterole,rolcreatedb,rolreplication,rolbypassrls,risky_roles,writable_tables,total_tables,create_schemas,create_db\n"
                + super + "," + createrole + "," + createdb + "," + repl + "," + bypass + "," + risky + "," + QString::number(writable) + "," + QString::number(total) + ","
                + QString::number(schemas) + "," + createDb + "\n").toUtf8();
    }
    static DbProfile pgProfile() {
        DbProfile p; p.engine = DbEngine::Postgres; p.host = "127.0.0.1"; p.port = 5432; p.user = "app"; p.database = "shop db"; return p;
    }

    void roleWithoutWriteRightsIsOk() {
        const RoleCheck r = assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", "", 0, 12, 0, "f"), "<<N>>");
        QVERIFY(r.ran);
        QCOMPARE(r.level, QString("ok"));
        QVERIFY(r.findings.isEmpty() && r.recommendation.isEmpty());
        QVERIFY(r.summary.contains("aucun droit d'écriture"));
        QVERIFY(r.summary.contains("effets de bord"));            // honest about what a catalogue query cannot see
    }
    void roleWithWriteRightsWarns() {
        const RoleCheck r = assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", "", 3, 12, 0, "f"), "<<N>>");
        QCOMPARE(r.level, QString("warning"));
        QVERIFY(r.findings.join("\n").contains("3 table(s) ou vue(s) sur 12"));
        QVERIFY(r.recommendation.contains("CREATE ROLE fortiq_ro"));
        QVERIFY(r.recommendation.contains("\"shop db\""));         // the database name is quoted as an identifier
        QVERIFY(r.recommendation.contains("ne l'exécute jamais"));
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", "", 0, 5, 1, "f"), "<<N>>").level, QString("warning"));          // may create in a schema
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", "", 0, 5, 0, "t"), "<<N>>").level, QString("warning"));          // may create schemas
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "t", "f", "f", "f", "", 0, 5, 0, "f"), "<<N>>").level, QString("warning"));          // createrole
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "t", "f", "f", "", 0, 5, 0, "f"), "<<N>>").level, QString("warning"));          // createdb
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "t", "f", "", 0, 5, 0, "f"), "<<N>>").level, QString("warning"));          // replication
    }
    void powerfulRolesAreDangerous() {
        const RoleCheck su = assessRoleCheck(pgProfile(), roleRow("t", "t", "t", "t", "t", "", 9, 9, 2, "t"), "<<N>>");
        QCOMPARE(su.level, QString("danger"));
        QVERIFY(su.findings.join("\n").contains("Superutilisateur"));
        QVERIFY(su.summary.contains("très étendus"));
        QVERIFY(!su.recommendation.isEmpty());
        for (const char *role : { "pg_write_server_files", "pg_execute_server_program", "pg_write_all_data" })
            QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", role, 0, 5, 0, "f"), "<<N>>").level, QString("danger"));
        const RoleCheck two = assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "f", "\"pg_read_server_files,pg_signal_backend\"", 0, 5, 0, "f"), "<<N>>");
        QCOMPARE(two.level, QString("warning"));                    // reading files / signalling sessions: a warning, not the worst level
        QCOMPARE(two.findings.size(), 2);
        QCOMPARE(assessRoleCheck(pgProfile(), roleRow("f", "f", "f", "f", "t", "", 0, 5, 0, "f"), "<<N>>").level, QString("note"));            // bypass RLS alone: a note
    }
    void unreadableRoleAnswerIsUnknownNotAnError() {
        for (const char *garbage : { "", "ERROR: something", "a,b\n1,2\n", "rolsuper\n" }) {
            const RoleCheck r = assessRoleCheck(pgProfile(), garbage, "<<N>>");
            QVERIFY2(!r.ran && r.level == "unknown", garbage);
            QVERIFY(r.summary.contains("non vérifiés"));
        }
    }
    void sqliteFileRights() {
        DbProfile p; p.engine = DbEngine::Sqlite; p.path = "/home/u/a.db";
        const RoleCheck w = assessRoleCheck(p, "file_writable=1\ndir_writable=1\nowner=u\nuser=u\nmode=644\n", QString());
        QVERIFY(w.ran); QCOMPARE(w.level, QString("note"));
        QVERIFY(w.findings.join("\n").contains("peut écrire") && w.findings.join("\n").contains("remplacé"));
        QVERIFY(w.recommendation.contains("lecture seule"));
        const RoleCheck ro = assessRoleCheck(p, "file_writable=0\ndir_writable=0\nowner=root\nuser=u\nmode=644\n", QString());
        QCOMPARE(ro.level, QString("ok"));
        QCOMPARE(assessRoleCheck(p, "nonsense", QString()).level, QString("unknown"));
        const QVariantMap map = roleCheckToVariant(w);
        QCOMPARE(map.value("level").toString(), QString("note"));
        QVERIFY(map.value("findings").toStringList().size() >= 1);
    }
    void roleScriptsAreFixedQueries() {
        DbProfile p = pgProfile();
        const QByteArray s = buildRoleCheckScript(p, "pw", { 10, 10 }, "<<N>>", "FQDELIMX");
        QVERIFY(s.contains("has_table_privilege") && s.contains("pg_has_role(current_user") && s.contains("rolsuper"));
        QVERIFY(s.contains("export PGPASSWORD='pw'"));                   // the password only as an environment assignment, as for queries
        QVERIFY(s.contains("default_transaction_read_only=on"));        // the check itself runs in the read-only session
        QVERIFY(!s.contains("insert") && !s.contains("update ") && !s.contains("create ") && !s.contains("alter "));
        DbProfile sq; sq.engine = DbEngine::Sqlite; sq.path = "/home/u/it's.db";
        const QByteArray f = buildRoleCheckScript(sq, QString(), { 10, 10 }, QString(), "D");
        QVERIFY(f.contains("f='/home/u/it'\\''s.db'") && f.contains("-w \"$f\"") && f.contains("stat -c %U"));
        QVERIFY(!f.contains("chmod") && !f.contains("sqlite3"));         // it only reads file rights
        QVERIFY(readOnlyRoleRecipe(p).contains("GRANT CONNECT ON DATABASE \"shop db\" TO fortiq_ro;"));
        DbProfile odd = pgProfile(); odd.database = "a\"b";
        QVERIFY(readOnlyRoleRecipe(odd).contains("\"a\"\"b\""));          // identifiers from the profile are never pasted raw
    }

    void limits() {
        const DbLimits l = clampLimits({ 0, 99999 });
        QCOMPARE(l.timeoutSec, 1); QCOMPARE(l.maxRows, 5000);
        const DbLimits m = clampLimits({ 999, -3 });
        QCOMPARE(m.timeoutSec, 120); QCOMPARE(m.maxRows, 1);
    }

    void sqliteScript() {
        DbProfile p; p.engine = DbEngine::Sqlite; p.path = "/home/u/it's.db";
        const QString d = randomToken();
        const QByteArray s = buildSqliteScript(p, "select 'a'", { 10, 100 }, d);
        QVERIFY(s.contains("sqlite3 -safe -readonly -bail -batch"));
        QVERIFY(s.contains("sqlite3 -safe :memory: 'select 1' >/dev/null 2>&1 || { echo 'FQ-ERR old-sqlite' >&2; exit 11; }"));   // fails closed on a shell without safe mode
        QVERIFY(s.contains("f='/home/u/it'\\''s.db'"));
        QVERIFY(s.contains("timeout -k 2 10 "));
        QVERIFY(s.contains("<<'" + d.toUtf8() + "'\nselect 'a'\n" + d.toUtf8() + "\n"));
        QVERIFY(buildSqliteScript(p, "select '" + d + "'", { 10, 100 }, d).isEmpty());   // delimiter collision
        QVERIFY(buildSqliteScript(p, "select 1", { 10, 100 }, QString()).isEmpty());
    }

    void postgresScript() {
        DbProfile p; p.engine = DbEngine::Postgres; p.host = "db.local"; p.port = 5433; p.user = "fqdb"; p.database = "fqtest";
        const QString d = randomToken(), nm = "<<N-" + randomToken(6) + ">>";
        const QByteArray s = buildPostgresScript(p, "pw'x", "select 1", { 7, 100 }, nm, d);
        QVERIFY(s.contains("export PGPASSWORD='pw'\\''x'"));
        QVERIFY(s.contains("default_transaction_read_only=on"));
        QVERIFY(s.contains("statement_timeout=7000"));
        QVERIFY(s.contains("timeout -k 2 12 psql -X -q --csv"));
        QVERIFY(s.contains("-v ON_ERROR_STOP=1"));
        QVERIFY(s.contains("-h 'db.local' -p 5433 -U 'fqdb' -d 'fqtest'"));
        // the password is only an environment assignment: never on the psql command line
        const int cmd = s.indexOf("psql -X");
        QVERIFY(!s.mid(cmd).contains("pw'"));
        const QByteArray socket = buildPostgresScript([] { DbProfile q; q.engine = DbEngine::Postgres; q.port = 5432; q.user = "u"; q.database = "d"; return q; }(), "", "select 1", { 7, 10 }, nm, d);
        QVERIFY(!socket.contains(" -h "));
        QVERIFY(!socket.contains("PGPASSWORD"));
    }

    void postgresCsv() {
        const QString nm = "<<N-1>>";
        const QByteArray csv = "id,name,note\n1,Zo\xC3\xAB,\"a \"\"q\"\", b\"\n2,<<N-1>>,\"\"\n3,x,\"line1\nline2\"\n";
        const DbResult r = parsePostgresCsv(csv, nm, 100, false);
        QVERIFY(r.ok);
        QCOMPARE(r.columns, QStringList({ "id", "name", "note" }));
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.rows[0][1].toString(), QString::fromUtf8("Zo\xC3\xAB"));
        QCOMPARE(r.rows[0][2].toString(), QString("a \"q\", b"));
        QVERIFY(r.rows[1][1].isNull());
        QVERIFY(!r.rows[1][2].isNull());
        QCOMPARE(r.rows[1][2].toString(), QString());
        QCOMPARE(r.rows[2][2].toString(), QString("line1\nline2"));
        QVERIFY(r.numeric[0]); QVERIFY(!r.numeric[1]);
        QVERIFY(!r.truncated);
    }

    // S-6: the wipe helpers never copy a secret and never leave a holder with a half-wiped buffer.
    void secretsAreWipedWithoutCopies() {
        char raw[32];
        memset(raw, 'S', sizeof raw);
        fortiq::secureWipe(raw, sizeof raw);
        for (const char c : raw) QCOMPARE(int(static_cast<const volatile char &>(c)), 0);

        QByteArray a(64, 'k');
        QByteArray b = a;                                    // shared
        const char *buffer = b.constData();
        fortiq::wipe(a);                                     // not the last holder: drops its reference, touches nothing
        QVERIFY(a.isEmpty());
        QCOMPARE(b.constData(), buffer);                     // no detach: no second copy of the secret was made
        QCOMPARE(b, QByteArray(64, 'k'));                    // and the other holder still has its data
        fortiq::wipe(b);
        QVERIFY(b.isEmpty());

        QString s = QString(40, QLatin1Char('p'));
        QString t = s;
        const QChar *chars = t.constData();
        fortiq::wipe(s);
        QCOMPARE(t.constData(), chars);
        QCOMPARE(t, QString(40, QLatin1Char('p')));
        fortiq::wipe(t);
        QVERIFY(t.isEmpty());

        fortiq::SecretString secret(QStringLiteral("hunter2"));
        QVERIFY(secret == QLatin1String("hunter2"));         // behaves like a QString
        secret.wipeNow();
        QVERIFY(secret.isEmpty());
        fortiq::SecretBytes bytes("hunter2");
        QVERIFY(bytes.contains("hunter"));
        bytes.wipeNow();
        QVERIFY(bytes.isEmpty());
    }

    // S-7: a 4 MiB answer of tiny rows must not be turned into millions of QVariants: parsing stops right after maxRows + 1 rows.
    void hugeResultsAreCutEarly() {
        QByteArray csv = "n\n";
        csv.reserve(4 * 1024 * 1024);
        while (csv.size() < 4 * 1024 * 1024) csv += "1\n";
        QElapsedTimer clock; clock.start();
        const DbResult r = parsePostgresCsv(csv, "<<N>>", 500, true);
        QCOMPARE(r.rows.size(), 500); QVERIFY(r.truncated); QVERIFY(r.ok);
        QVERIFY2(r.totalParsed <= 501, "the parser kept going after the limit");
        QVERIFY2(clock.elapsed() < 2000, qPrintable(QString::number(clock.elapsed()) + " ms"));
        QByteArray q = "n\n";
        while (q.size() < 4 * 1024 * 1024) q += "1\n";
        const DbResult s = parseSqliteQuote(q, 500, true);
        QCOMPARE(s.rows.size(), 500); QVERIFY(s.truncated);
        QVERIFY2(s.totalParsed <= 501, "the SQLite parser kept going after the limit");
        // One cell as large as the whole cap, and a very wide row: bounded by the byte cap, nothing quadratic.
        QByteArray fat = "x\n\"" + QByteArray(4 * 1024 * 1024 - 8, 'a') + "\"\n";
        clock.restart();
        const DbResult f = parsePostgresCsv(fat, "<<N>>", 500, false);
        QCOMPARE(f.rows.size(), 1); QVERIFY(f.rows[0][0].toString().size() > 4000000);
        QVERIFY2(clock.elapsed() < 3000, qPrintable(QString::number(clock.elapsed()) + " ms"));
        QByteArray wide = "c0";
        for (int i = 1; i < 20000; ++i) wide += ",c" + QByteArray::number(i);
        wide += "\n1";
        for (int i = 1; i < 20000; ++i) wide += ",1";
        wide += "\n";
        const DbResult w = parsePostgresCsv(wide, "<<N>>", 500, false);
        QCOMPARE(w.columns.size(), 20000); QCOMPARE(w.rows.size(), 1);
        // Cut inside a quoted field (the output cap hit mid-cell): flagged, no partial row.
        const DbResult cut = parsePostgresCsv("a\n\"unfinished", "<<N>>", 500, true);
        QCOMPARE(cut.rows.size(), 0); QVERIFY(cut.truncated);
    }

    void postgresCsvLimits() {
        QByteArray csv = "n\n";
        for (int i = 0; i < 50; ++i) csv += QByteArray::number(i) + "\n";
        const DbResult r = parsePostgresCsv(csv, "<<N>>", 10, false);
        QCOMPARE(r.rows.size(), 10);
        QVERIFY(r.truncated);
        // output cut in the middle of a row: the partial row is dropped and flagged
        const DbResult c = parsePostgresCsv("a,b\n1,2\n3,", "<<N>>", 10, true);
        QCOMPARE(c.rows.size(), 1);
        QVERIFY(c.truncated);
        const DbResult e = parsePostgresCsv("", "<<N>>", 10, false);
        QVERIFY(e.ok); QVERIFY(e.columns.isEmpty());
        const DbResult h = parsePostgresCsv("a,b\n", "<<N>>", 10, false);
        QCOMPARE(h.columns.size(), 2); QCOMPARE(h.rows.size(), 0);
        const DbResult crlf = parsePostgresCsv("a\r\n1\r\n", "<<N>>", 10, false);
        QCOMPARE(crlf.rows.size(), 1); QCOMPARE(crlf.rows[0][0].toString(), QString("1"));
    }

    void sqliteQuote() {
        const QByteArray q = "id,name,note,score,b,we ird\n"
                             "1,'Zo\xC3\xAB','it''s, a \"q\"',1.5,X'00ff10','x'\n"
                             "2,'\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD',NULL,NULL,NULL,'multi\nline'\n"
                             "3,'','',0,X'','NULL'\n";
        const DbResult r = parseSqliteQuote(q, 100, false);
        QVERIFY(r.ok);
        QCOMPARE(r.columns.size(), 6);
        QCOMPARE(r.columns[5], QString("we ird"));
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.rows[0][2].toString(), QString("it's, a \"q\""));
        QCOMPARE(r.rows[0][3].toString(), QString("1.5"));
        QCOMPARE(r.rows[0][4].toString(), QString("X'00ff10'"));
        QVERIFY(r.rows[1][2].isNull());
        QCOMPARE(r.rows[1][5].toString(), QString("multi\nline"));
        QVERIFY(!r.rows[2][2].isNull());
        QCOMPARE(r.rows[2][2].toString(), QString());
        QVERIFY(!r.rows[2][5].isNull());   // the string 'NULL' is not NULL
        QCOMPARE(r.rows[2][5].toString(), QString("NULL"));
    }

    void sqliteQuoteLimits() {
        QByteArray q = "n\n";
        for (int i = 0; i < 30; ++i) q += QByteArray::number(i) + "\n";
        const DbResult r = parseSqliteQuote(q, 5, false);
        QCOMPARE(r.rows.size(), 5); QVERIFY(r.truncated);
        const DbResult c = parseSqliteQuote("a,b\n1,'unfinish", 5, true);
        QCOMPARE(c.rows.size(), 0); QVERIFY(c.truncated);
        const DbResult none = parseSqliteQuote("", 5, false);
        QVERIFY(none.ok); QVERIFY(none.columns.isEmpty());
    }

    void failures() {
        QVERIFY(describeDbFailure(DbEngine::Sqlite, 4, "FQ-ERR missing-sqlite3", 10).contains("sqlite3"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 4, "FQ-ERR missing-psql", 10).contains("psql"));
        QVERIFY(describeDbFailure(DbEngine::Sqlite, 5, "FQ-ERR no-file", 10).contains("n'existe pas"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 124, "", 15).contains("15"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 1, "ERROR:  canceling statement due to statement timeout", 15).contains("Délai"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 1, "ERROR:  cannot execute INSERT in a read-only transaction", 15).contains("lecture seule"));
        QVERIFY(describeDbFailure(DbEngine::Sqlite, 8, "Error: attempt to write a readonly database", 15).contains("lecture seule"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 2, "psql: error: connection to server failed: FATAL:  password authentication failed for user \"x\"", 15).contains("Authentification"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 2, "psql: error: connection to server at \"127.0.0.1\", port 1 failed: Connection refused", 15).contains("Connexion impossible"));
        QVERIFY(describeDbFailure(DbEngine::Postgres, 1, "ERROR:  syntax error at or near \"selct\"\nLINE 1: selct 1", 15).contains("syntax error"));
        QVERIFY(!describeDbFailure(DbEngine::Postgres, 9, "", 15).isEmpty());
    }

    void schemaParsing() {
        const QByteArray sq = "tbl,kind,col,typ,nn\n'people','table','id','INTEGER',0\n'people','table','name','TEXT',1\n'v','view','x','',0\n'empty','table',NULL,NULL,NULL\n";
        const QList<DbObject> a = parseSchema(DbEngine::Sqlite, sq, QString());
        QCOMPARE(a.size(), 3);
        QCOMPARE(a[0].name, QString("people")); QCOMPARE(a[0].columns.size(), 2);
        QVERIFY(a[0].columns[1].toMap().value("nullable").toBool() == false);
        QCOMPARE(a[1].kind, QString("view"));
        QCOMPARE(a[2].columns.size(), 0);
        const QByteArray pg = "table_schema,table_name,table_type,column_name,data_type,is_nullable\npublic,people,BASE TABLE,id,integer,NO\npublic,people,BASE TABLE,name,text,YES\napp,v,VIEW,x,text,YES\n";
        const QList<DbObject> b = parseSchema(DbEngine::Postgres, pg, "<<N>>");
        QCOMPARE(b.size(), 2);
        QCOMPARE(schemaToVariant(b)[1].toMap().value("label").toString(), QString("app.v"));
        QCOMPARE(schemaToVariant(b)[0].toMap().value("label").toString(), QString("people"));
        const QByteArray script = buildSchemaScript([] { DbProfile q; q.engine = DbEngine::Postgres; q.user = "u"; q.database = "d"; return q; }(), "", { 10, 10 }, "<<N>>", "DELIM");
        QVERIFY(script.contains("information_schema.columns"));
    }

    void identifiers() {
        QCOMPARE(quoteIdentifier("a\"b"), QString("\"a\"\"b\""));
        QCOMPARE(cellText(QVariant(), 10), QString());
        QCOMPARE(cellText(QString(30, 'x'), 5).size(), 6);
    }

    void tokens() {
        QCOMPARE(randomToken(12).size(), 24);
        QVERIFY(randomToken() != randomToken());
    }
};

QTEST_APPLESS_MAIN(DbTest)
#include "DbTest.moc"
