#include "DbInfo.h"

#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace fortiq {

QString engineName(DbEngine e) { return e == DbEngine::Sqlite ? QStringLiteral("sqlite") : QStringLiteral("postgres"); }

namespace {
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}
bool hasControl(const QString &s) {
    for (const QChar c : s) if (c.unicode() < 0x20 || c.unicode() == 0x7f) return true;
    return false;
}
bool isIdentChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); }
}  // namespace

// ---------------------------------------------------------------- profiles
QString validateProfile(const DbProfile &p) {
    const QString name = p.name.trimmed();
    if (name.isEmpty() || name.size() > 60 || hasControl(name)) return QStringLiteral("Donnez un nom (1 à 60 caractères).");
    if (p.engine == DbEngine::Sqlite) {
        if (!p.path.startsWith(QLatin1Char('/')) || p.path.size() > 1024 || hasControl(p.path)) return QStringLiteral("Le fichier SQLite doit être un chemin absolu sur le serveur.");
        return {};
    }
    static const QRegularExpression host(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,252}$"));
    static const QRegularExpression user(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.$@-]{0,62}$"));
    static const QRegularExpression database(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.$-]{0,62}$"));
    if (!p.host.isEmpty() && !host.match(p.host).hasMatch()) return QStringLiteral("Hôte invalide (lettres, chiffres, points, tirets ; vide = socket local).");
    if (p.port < 1 || p.port > 65535) return QStringLiteral("Port invalide (1–65535).");
    if (!user.match(p.user).hasMatch()) return QStringLiteral("Utilisateur invalide.");
    if (!database.match(p.database).hasMatch()) return QStringLiteral("Nom de base invalide.");
    return {};
}

QVariantMap profileToVariant(const DbProfile &p) {
    return QVariantMap { { "id", p.id }, { "serverId", p.serverId }, { "name", p.name }, { "engine", engineName(p.engine) }, { "path", p.path },
                         { "host", p.host }, { "port", p.port }, { "user", p.user }, { "database", p.database }, { "hasPassword", p.hasPassword },
                         { "summary", p.engine == DbEngine::Sqlite ? p.path
                              : p.user + QLatin1Char('@') + (p.host.isEmpty() ? QStringLiteral("socket local") : p.host) + QLatin1Char('/') + p.database } };
}

DbProfile profileFromVariant(const QVariantMap &m) {
    DbProfile p;
    p.id = m.value("id").toString();
    p.serverId = m.value("serverId").toString();
    p.name = m.value("name").toString().trimmed();
    p.engine = m.value("engine").toString() == QLatin1String("postgres") ? DbEngine::Postgres : DbEngine::Sqlite;
    p.path = m.value("path").toString().trimmed();
    p.host = m.value("host").toString().trimmed();
    p.port = m.contains("port") ? m.value("port").toInt() : 5432;
    p.user = m.value("user").toString().trimmed();
    p.database = m.value("database").toString().trimmed();
    p.hasPassword = m.value("hasPassword").toBool();
    return p;
}

// ---------------------------------------------------------------- SQL scanning
SqlScan scanSql(const QString &sql, DbEngine engine) {
    enum State { Normal, Single, Double, Line, Block, Dollar, Bracket, Backtick };
    SqlScan r;
    const bool pg = engine == DbEngine::Postgres;
    State st = Normal;
    bool content = false, lineStart = true, eString = false, keywordDone = false;
    int blockDepth = 0;
    QString dollarTag, kw, word;
    const qsizetype n = sql.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = sql.at(i);
        const QChar next = i + 1 < n ? sql.at(i + 1) : QChar();
        switch (st) {
        case Normal:
            if (c.isLetterOrNumber() || c == QLatin1Char('_')) word += c.toLower();
            else if (!word.isEmpty()) { r.words << word; word.clear(); }
            if (c.isSpace()) { if (c == QLatin1Char('\n')) lineStart = true; if (!kw.isEmpty()) keywordDone = true; break; }
            if (lineStart && !pg && c == QLatin1Char('.')) r.metaCommand = true;   // sqlite3 shell dot-command (.shell, .read, .import ...)
            lineStart = false;
            if (c == QLatin1Char('-') && next == QLatin1Char('-')) { st = Line; ++i; if (!kw.isEmpty()) keywordDone = true; break; }
            if (c == QLatin1Char('/') && next == QLatin1Char('*')) { st = Block; blockDepth = 1; ++i; if (!kw.isEmpty()) keywordDone = true; break; }
            if (c == QLatin1Char(';')) { if (content) ++r.statements; content = false; keywordDone = keywordDone || !kw.isEmpty(); break; }
            if (c == QLatin1Char('\\') && pg) { r.metaCommand = true; content = true; break; }
            // everything below is statement content
            if (!keywordDone) {
                if (c.isLetter()) kw += c.toLower();
                else if (c == QLatin1Char('(') && kw.isEmpty()) {}
                else keywordDone = true;
            }
            content = true;
            if (c == QLatin1Char('\'')) {
                st = Single;
                eString = pg && i > 0 && (sql.at(i - 1) == QLatin1Char('e') || sql.at(i - 1) == QLatin1Char('E')) && (i < 2 || !isIdentChar(sql.at(i - 2)));
            } else if (c == QLatin1Char('"')) st = Double;
            else if (c == QLatin1Char('`') && !pg) st = Backtick;
            else if (c == QLatin1Char('[') && !pg) st = Bracket;
            else if (c == QLatin1Char('$') && pg && (i == 0 || !isIdentChar(sql.at(i - 1)))) {
                qsizetype j = i + 1;
                if (j < n && (sql.at(j).isLetter() || sql.at(j) == QLatin1Char('_'))) { while (j < n && (sql.at(j).isLetterOrNumber() || sql.at(j) == QLatin1Char('_'))) ++j; }
                if (j < n && sql.at(j) == QLatin1Char('$')) { dollarTag = sql.mid(i, j - i + 1); st = Dollar; i = j; }
            }
            break;
        case Single:
            if (c == QLatin1Char('\'')) { if (next == QLatin1Char('\'')) ++i; else st = Normal; }
            else if (eString && c == QLatin1Char('\\')) ++i;
            break;
        case Double:
            if (c == QLatin1Char('"')) { if (next == QLatin1Char('"')) ++i; else st = Normal; }
            break;
        case Backtick:
            if (c == QLatin1Char('`')) { if (next == QLatin1Char('`')) ++i; else st = Normal; }
            break;
        case Bracket:
            if (c == QLatin1Char(']')) st = Normal;
            break;
        case Line:
            if (c == QLatin1Char('\n')) { st = Normal; lineStart = true; }
            break;
        case Block:
            if (pg && c == QLatin1Char('/') && next == QLatin1Char('*')) { ++blockDepth; ++i; }
            else if (c == QLatin1Char('*') && next == QLatin1Char('/')) { ++i; if (--blockDepth == 0) st = Normal; }
            break;
        case Dollar:
            if (c == QLatin1Char('$') && sql.mid(i, dollarTag.size()) == dollarTag) { i += dollarTag.size() - 1; st = Normal; }
            break;
        }
    }
    if (!word.isEmpty()) r.words << word;
    if (content) ++r.statements;
    r.unterminated = st != Normal && st != Line;
    r.firstKeyword = kw;
    return r;
}

QString readOnlyRefusal(const QString &sql, DbEngine engine) {
    if (sql.trimmed().isEmpty()) return QStringLiteral("Saisissez une requête SQL.");
    if (sql.size() > 100 * 1024) return QStringLiteral("Requête trop longue (limite 100 Kio).");
    for (const QChar c : sql) if (c == QChar::Null) return QStringLiteral("La requête contient un octet nul.");
    const SqlScan s = scanSql(sql, engine);
    if (s.unterminated) return QStringLiteral("Requête incomplète : guillemet, commentaire ou bloc non fermé.");
    if (s.metaCommand)
        return engine == DbEngine::Postgres ? QStringLiteral("Les commandes psql (\\…) sont refusées : seul du SQL est accepté.")
                                            : QStringLiteral("Les commandes du shell sqlite3 (lignes commençant par un point) sont refusées : seul du SQL est accepté.");
    if (s.statements != 1) return QStringLiteral("Un seul ordre SQL à la fois (%1 détectés).").arg(s.statements);
    // Defence in depth, not the guard: functions that touch files, other sessions or the server configuration. The real guards are
    // `sqlite3 -safe -readonly`, the read-only PostgreSQL session and, above all, a database role without those rights.
    static const QSet<QString> sqliteDenied { "readfile", "writefile", "edit", "load_extension", "fsdir", "fts3_tokenizer" };
    static const QSet<QString> pgDenied {
        "pg_terminate_backend", "pg_cancel_backend", "pg_reload_conf", "pg_rotate_logfile", "pg_switch_wal", "pg_promote", "pg_read_file", "pg_read_binary_file",
        "pg_ls_dir", "pg_stat_file", "pg_ls_logdir", "pg_ls_waldir", "lo_import", "lo_export", "lo_create", "lo_unlink", "lo_put", "dblink", "dblink_exec",
        "dblink_connect", "dblink_connect_u", "set_config", "pg_create_restore_point", "pg_wal_replay_pause", "pg_wal_replay_resume", "pg_logical_emit_message",
        "pg_drop_replication_slot", "pg_create_physical_replication_slot", "pg_create_logical_replication_slot", "pg_replication_origin_create" };
    for (const QString &w : s.words)
        if ((engine == DbEngine::Sqlite ? sqliteDenied : pgDenied).contains(w))
            return QStringLiteral("La fonction « %1 » est refusée en mode lecture seule (accès aux fichiers du serveur, aux autres sessions ou à la configuration).").arg(w);
    static const QSet<QString> pgAllowed { "select", "with", "values", "table", "show", "explain" };
    static const QSet<QString> sqliteAllowed { "select", "with", "values", "explain", "pragma" };
    const bool ok = engine == DbEngine::Postgres ? pgAllowed.contains(s.firstKeyword) : sqliteAllowed.contains(s.firstKeyword);
    if (!ok)
        return QStringLiteral("Mode lecture seule : seuls %1 sont acceptés (« %2 » refusé).")
            .arg(engine == DbEngine::Postgres ? QStringLiteral("SELECT, WITH, VALUES, TABLE, SHOW et EXPLAIN") : QStringLiteral("SELECT, WITH, VALUES, EXPLAIN et PRAGMA"),
                 s.firstKeyword.isEmpty() ? QStringLiteral("?") : s.firstKeyword.toUpper());
    return {};
}

DbLimits clampLimits(DbLimits l) {
    l.timeoutSec = qBound(1, l.timeoutSec, 120);
    l.maxRows = qBound(1, l.maxRows, 5000);
    return l;
}

QString randomToken(int bytes) {
    QString out;
    for (int i = 0; i < bytes; ++i) out += QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(256), 2, 16, QLatin1Char('0'));
    return out;
}

// ---------------------------------------------------------------- scripts
QByteArray buildSqliteScript(const DbProfile &p, const QString &sql, const DbLimits &limitsIn, const QString &delimiter) {
    if (delimiter.isEmpty() || sql.contains(delimiter)) return {};
    const DbLimits limits = clampLimits(limitsIn);
    const QByteArray d = delimiter.toUtf8();
    QByteArray s = "export LC_ALL=C.UTF-8\nf=" + shQuote(p.path) + "\n";
    s += "command -v sqlite3 >/dev/null 2>&1 || { echo 'FQ-ERR missing-sqlite3' >&2; exit 4; }\n"
         "sqlite3 -safe :memory: 'select 1' >/dev/null 2>&1 || { echo 'FQ-ERR old-sqlite' >&2; exit 11; }\n"
         "[ -f \"$f\" ] || { echo 'FQ-ERR no-file' >&2; exit 5; }\n"
         "[ -r \"$f\" ] || { echo 'FQ-ERR unreadable' >&2; exit 6; }\n";
    s += "timeout -k 2 " + QByteArray::number(limits.timeoutSec) + " sqlite3 -safe -readonly -bail -batch -cmd '.timeout 2000' -cmd '.headers on' -cmd '.mode quote' \"$f\" <<'" + d + "'\n";
    s += sql.toUtf8();
    if (!sql.endsWith(QLatin1Char('\n'))) s += '\n';
    s += d + "\n";
    return s;
}

namespace {
// Appends `'value'` (single-quote safe) for a SECRET value straight into the destination, byte by byte: no intermediate
// QByteArray ever holds the password (S-6). The destination must have its capacity reserved so that it never reallocates.
void appendSecretQuoted(SecretBytes &out, const QString &value) {
    const SecretBytes bytes = value.toUtf8();
    out += '\'';
    for (const char ch : bytes) {
        if (ch == '\'') out += "'\\''"; else out += ch;
    }
    out += '\'';
}
// The script is built in ONE buffer reserved up front: a growing buffer would leave its old, password-bearing copies on the heap.
SecretBytes newScriptBuffer(const QString &password, qsizetype rest) {
    SecretBytes s;
    s.reserve(static_cast<qsizetype>(password.size()) * 12 + rest + 8192);   // UTF-8 is at most 3 bytes per UTF-16 unit, and every quote grows to 4 bytes (no temporary copy of the secret to measure it)
    return s;
}
void pgPrologue(SecretBytes &s, const DbProfile &p, const QString &password, const DbLimits &limits) {
    s += "export LC_ALL=C.UTF-8 PGCONNECT_TIMEOUT=8\n";
    if (!password.isEmpty()) { s += "export PGPASSWORD="; appendSecretQuoted(s, password); s += "\n"; }
    s += "export PGOPTIONS='-c default_transaction_read_only=on -c statement_timeout=" + QByteArray::number(limits.timeoutSec * 1000)
       + " -c lock_timeout=3000 -c idle_in_transaction_session_timeout=" + QByteArray::number(limits.timeoutSec * 1000)
       + " -c application_name=FORTIQ'\n";
    s += "command -v psql >/dev/null 2>&1 || { echo 'FQ-ERR missing-psql' >&2; exit 4; }\n";
}
QByteArray pgCommand(const DbProfile &p, const DbLimits &limits, const QString &nullMarker) {
    QByteArray c = "timeout -k 2 " + QByteArray::number(limits.timeoutSec + 5) + " psql -X -q --csv -P null=" + shQuote(nullMarker)
                 + " -v ON_ERROR_STOP=1 -v FETCH_COUNT=200";
    if (!p.host.isEmpty()) c += " -h " + shQuote(p.host);
    c += " -p " + QByteArray::number(p.port) + " -U " + shQuote(p.user) + " -d " + shQuote(p.database);
    return c;
}
}  // namespace

SecretBytes buildPostgresScript(const DbProfile &p, const QString &password, const QString &sql, const DbLimits &limitsIn,
                               const QString &nullMarker, const QString &delimiter) {
    if (delimiter.isEmpty() || sql.contains(delimiter)) return {};
    const DbLimits limits = clampLimits(limitsIn);
    SecretBytes s = newScriptBuffer(password, static_cast<qsizetype>(sql.size()) * 4 + 2048);
    pgPrologue(s, p, password, limits);
    s += pgCommand(p, limits, nullMarker) + " <<'" + delimiter.toUtf8() + "'\n";
    s += sql.toUtf8();
    if (!sql.endsWith(QLatin1Char('\n'))) s += '\n';
    s += delimiter.toUtf8() + "\n";
    return s;
}

SecretBytes buildSchemaScript(const DbProfile &p, const QString &password, const DbLimits &limitsIn, const QString &nullMarker, const QString &delimiter) {
    const DbLimits limits = clampLimits(limitsIn);
    const QByteArray d = delimiter.toUtf8();
    if (p.engine == DbEngine::Sqlite) {
        SecretBytes s = "export LC_ALL=C.UTF-8\nf=" + shQuote(p.path) + "\n";
        s += "command -v sqlite3 >/dev/null 2>&1 || { echo 'FQ-ERR missing-sqlite3' >&2; exit 4; }\n"
             "sqlite3 -safe :memory: 'select 1' >/dev/null 2>&1 || { echo 'FQ-ERR old-sqlite' >&2; exit 11; }\n"
             "[ -r \"$f\" ] || { echo 'FQ-ERR unreadable' >&2; exit 6; }\n";
        s += "timeout -k 2 " + QByteArray::number(limits.timeoutSec) + " sqlite3 -safe -readonly -bail -batch -cmd '.timeout 2000' -cmd '.headers on' -cmd '.mode quote' \"$f\" <<'" + d + "'\n"
             "select m.name as tbl, m.type as kind, p.name as col, p.type as typ, p.\"notnull\" as nn from sqlite_master m left join pragma_table_info(m.name) p "
             "where m.type in ('table','view') and m.name not like 'sqlite_%' order by m.name, p.cid;\n" + d + "\n";
        return s;
    }
    SecretBytes s = newScriptBuffer(password, 4096);
    pgPrologue(s, p, password, limits);
    s += pgCommand(p, limits, nullMarker) + " <<'" + d + "'\n"
         "select c.table_schema, c.table_name, t.table_type, c.column_name, c.data_type, c.is_nullable from information_schema.columns c "
         "join information_schema.tables t on t.table_schema = c.table_schema and t.table_name = c.table_name "
         "where c.table_schema not in ('pg_catalog','information_schema') order by c.table_schema, c.table_name, c.ordinal_position;\n" + d + "\n";
    return s;
}

SecretBytes buildRoleCheckScript(const DbProfile &p, const QString &password, const DbLimits &limitsIn, const QString &nullMarker, const QString &delimiter) {
    const DbLimits limits = clampLimits(limitsIn);
    const QByteArray d = delimiter.toUtf8();
    if (p.engine == DbEngine::Sqlite) {
        // File rights of the SSH user on the database file: key=value lines.
        SecretBytes s = "export LC_ALL=C.UTF-8\nf=" + shQuote(p.path) + "\n";
        s += "[ -e \"$f\" ] || { echo 'FQ-ERR no-file' >&2; exit 5; }\n"
             "if [ -w \"$f\" ]; then echo 'file_writable=1'; else echo 'file_writable=0'; fi\n"
             "if [ -w \"$(dirname -- \"$f\")\" ]; then echo 'dir_writable=1'; else echo 'dir_writable=0'; fi\n"
             "echo \"owner=$(stat -c %U -- \"$f\" 2>/dev/null)\"\n"
             "echo \"user=$(id -un)\"\n"
             "echo \"mode=$(stat -c %a -- \"$f\" 2>/dev/null)\"\n";
        return s;
    }
    const QByteArray tables = " from pg_class c join pg_namespace n on n.oid = c.relnamespace where c.relkind in ('r','p','v','f') "
                              "and n.nspname not in ('pg_catalog','information_schema') and n.nspname not like 'pg_toast%'";
    SecretBytes s = newScriptBuffer(password, 8192);
    pgPrologue(s, p, password, limits);
    s += pgCommand(p, limits, nullMarker) + " <<'" + d + "'\n"
         "select r.rolsuper, r.rolcreaterole, r.rolcreatedb, r.rolreplication, r.rolbypassrls, "
         "coalesce((select string_agg(x.rolname, ',' order by x.rolname) from pg_roles x where x.rolname in "
         "('pg_write_server_files','pg_read_server_files','pg_execute_server_program','pg_write_all_data','pg_signal_backend','pg_checkpoint','pg_maintain') "
         "and pg_has_role(current_user, x.oid, 'USAGE')), '') as risky_roles, "
         "(select count(*)" + tables + " and (has_table_privilege(c.oid,'INSERT') or has_table_privilege(c.oid,'UPDATE') or has_table_privilege(c.oid,'DELETE') or has_table_privilege(c.oid,'TRUNCATE'))) as writable_tables, "
         "(select count(*)" + tables + ") as total_tables, "
         "(select count(*) from pg_namespace n where n.nspname not in ('pg_catalog','information_schema') and n.nspname not like 'pg_toast%' and n.nspname not like 'pg_temp%' and has_schema_privilege(n.oid,'CREATE')) as create_schemas, "
         "has_database_privilege(current_database(),'CREATE') as create_db "
         "from pg_roles r where r.rolname = current_user;\n" + d + "\n";
    return s;
}

QString readOnlyRoleRecipe(const DbProfile &p) {
    if (p.engine == DbEngine::Sqlite)
        return QStringLiteral("Pour une garantie réelle : donnez à l'utilisateur SSH de FORTIQ un accès en lecture seule au fichier (par exemple un groupe qui ne peut que lire, ou une copie du fichier), ou travaillez sur une copie.");
    const QString db = quoteIdentifier(p.database.isEmpty() ? QStringLiteral("votre_base") : p.database);
    return QStringLiteral(
        "-- À exécuter par un administrateur PostgreSQL. FORTIQ ne l'exécute jamais.\n"
        "CREATE ROLE fortiq_ro LOGIN PASSWORD 'CHANGEZ-MOI';\n"
        "GRANT CONNECT ON DATABASE %1 TO fortiq_ro;\n"
        "GRANT pg_read_all_data TO fortiq_ro;                 -- PostgreSQL 14 et plus. Avant : GRANT USAGE ON SCHEMA public TO fortiq_ro;\n"
        "                                                     --   GRANT SELECT ON ALL TABLES IN SCHEMA public TO fortiq_ro;\n"
        "ALTER ROLE fortiq_ro SET default_transaction_read_only = on;\n"
        "REVOKE CREATE ON SCHEMA public FROM PUBLIC;          -- PostgreSQL 14 et avant : le schéma public est inscriptible par tous\n"
        "-- Puis créez un profil FORTIQ avec l'utilisateur fortiq_ro.").arg(db);
}

RoleCheck assessRoleCheck(const DbProfile &p, const QByteArray &output, const QString &nullMarker) {
    RoleCheck r;
    auto unknown = [&](const QString &why) { r.ran = false; r.level = QStringLiteral("unknown"); r.summary = QStringLiteral("Droits du rôle non vérifiés : %1").arg(why); return r; };
    if (p.engine == DbEngine::Sqlite) {
        QHash<QString, QString> kv;
        for (const QByteArray &line : output.split('\n')) {
            const qsizetype eq = line.indexOf('=');
            if (eq > 0) kv.insert(QString::fromUtf8(line.left(eq)).trimmed(), QString::fromUtf8(line.mid(eq + 1)).trimmed());
        }
        if (!kv.contains("file_writable")) return unknown(QStringLiteral("réponse illisible."));
        r.ran = true;
        const bool writable = kv.value("file_writable") == QLatin1String("1"), dirWritable = kv.value("dir_writable") == QLatin1String("1");
        if (writable) {
            r.level = QStringLiteral("note");
            r.summary = QStringLiteral("Le fichier est modifiable par l'utilisateur SSH : le mode lecture seule de FORTIQ est une protection locale.");
            r.findings << QStringLiteral("Propriétaire %1, droits %2 : l'utilisateur « %3 » peut écrire dans ce fichier.").arg(kv.value("owner"), kv.value("mode"), kv.value("user"));
            if (dirWritable) r.findings << QStringLiteral("Le dossier est aussi modifiable (le fichier peut être remplacé).");
            r.recommendation = readOnlyRoleRecipe(p);
        } else {
            r.level = QStringLiteral("ok");
            r.summary = QStringLiteral("Le fichier est en lecture seule pour l'utilisateur SSH : aucune écriture n'est possible, même en dehors de FORTIQ.");
        }
        return r;
    }
    const DbResult t = parsePostgresCsv(output, nullMarker, 5, false);
    if (!t.ok || t.rows.isEmpty()) return unknown(QStringLiteral("réponse vide (le rôle n'est pas visible dans pg_roles ?)."));
    auto col = [&](const char *name) { return t.columns.indexOf(QLatin1String(name)); };
    const QVariantList row = t.rows.first();
    auto text = [&](const char *name) { const int i = col(name); return i >= 0 && i < row.size() ? row.at(i).toString() : QString(); };
    auto flag = [&](const char *name) { return text(name) == QLatin1String("t"); };
    if (col("rolsuper") < 0 || col("writable_tables") < 0) return unknown(QStringLiteral("colonnes inattendues."));
    r.ran = true;
    int rank = 0;   // 0 ok, 1 note, 2 warning, 3 danger
    auto add = [&](int level, const QString &line) { rank = qMax(rank, level); r.findings << line; };
    if (flag("rolsuper")) add(3, QStringLiteral("Superutilisateur PostgreSQL : accès à tout, y compris, selon la configuration, aux fichiers et aux programmes du serveur de base."));
    const QStringList risky = text("risky_roles").split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &role : risky) {
        const bool hard = role == QLatin1String("pg_write_server_files") || role == QLatin1String("pg_execute_server_program") || role == QLatin1String("pg_write_all_data");
        add(hard ? 3 : 2, QStringLiteral("Membre de %1 (%2).").arg(role, hard ? QStringLiteral("écriture de données, de fichiers ou exécution de programmes") : QStringLiteral("droit étendu sur le serveur")));
    }
    const int writable = text("writable_tables").toInt(), total = text("total_tables").toInt(), schemas = text("create_schemas").toInt();
    if (writable > 0) add(2, QStringLiteral("Droit d'écriture (INSERT, UPDATE, DELETE ou TRUNCATE) sur %1 table(s) ou vue(s) sur %2.").arg(writable).arg(total));
    if (schemas > 0) add(2, QStringLiteral("Peut créer des objets dans %1 schéma(s).").arg(schemas));
    if (flag("create_db")) add(2, QStringLiteral("Peut créer des schémas dans cette base."));
    if (flag("rolcreaterole")) add(2, QStringLiteral("Peut créer des rôles."));
    if (flag("rolcreatedb")) add(2, QStringLiteral("Peut créer des bases."));
    if (flag("rolreplication")) add(2, QStringLiteral("Droit de réplication."));
    if (flag("rolbypassrls")) add(1, QStringLiteral("Ignore la sécurité par ligne (row level security)."));
    if (rank == 0) {
        r.level = QStringLiteral("ok");
        r.summary = QStringLiteral("Ce rôle n'a aucun droit d'écriture visible (tables, schémas, rôles). Des fonctions ou extensions peuvent encore avoir des effets de bord.");
        return r;
    }
    r.level = rank >= 3 ? QStringLiteral("danger") : rank == 2 ? QStringLiteral("warning") : QStringLiteral("note");
    r.summary = rank >= 3 ? QStringLiteral("Ce rôle a des droits très étendus : le mode lecture seule de FORTIQ n'est qu'une protection locale.")
              : rank == 2 ? QStringLiteral("Ce rôle peut écrire dans la base : le mode lecture seule de FORTIQ n'est qu'une protection locale.")
                          : QStringLiteral("Droit particulier noté pour ce rôle.");
    if (rank >= 2) r.recommendation = readOnlyRoleRecipe(p);
    return r;
}

QVariantMap roleCheckToVariant(const RoleCheck &c) {
    return QVariantMap { { "ran", c.ran }, { "level", c.level }, { "summary", c.summary }, { "findings", c.findings }, { "recommendation", c.recommendation } };
}

// ---------------------------------------------------------------- parsing
namespace {
bool looksNumeric(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^-?\\d+(\\.\\d+)?([eE][-+]?\\d+)?$"));
    return re.match(s).hasMatch();
}
void finishColumns(DbResult &r) {
    r.numeric.clear();
    for (int c = 0; c < r.columns.size(); ++c) {
        bool numeric = !r.rows.isEmpty(), any = false;
        for (const QVariantList &row : r.rows) {
            if (c >= row.size() || row.at(c).isNull()) continue;
            any = true;
            if (!looksNumeric(row.at(c).toString())) { numeric = false; break; }
        }
        r.numeric << (numeric && any);
    }
}
}  // namespace

DbResult parsePostgresCsv(const QByteArray &data, const QString &nullMarker, int maxRows, bool incomplete) {
    DbResult r;
    QStringDecoder dec(QStringDecoder::Utf8);
    const QString text = dec(data);
    QList<QVariantList> all;
    QVariantList row;
    QString field;
    bool inQuotes = false, quotedField = false, rowOpen = false;
    const qsizetype n = text.size();
    auto endField = [&] {
        row << ((!quotedField && field == nullMarker) ? QVariant() : QVariant(field));
        field.clear(); quotedField = false; rowOpen = true;
    };
    bool enough = false;   // header + maxRows rows + one more (proof of truncation): the rest of a 4 MiB answer is never materialized
    auto endRow = [&] { all << row; row.clear(); rowOpen = false; if (all.size() > static_cast<qsizetype>(maxRows) + 1) enough = true; };
    for (qsizetype i = 0; i < n && !enough; ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) { if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) { field += c; ++i; } else inQuotes = false; }
            else field += c;
            continue;
        }
        if (c == QLatin1Char('"') && field.isEmpty()) { inQuotes = true; quotedField = true; rowOpen = true; }
        else if (c == QLatin1Char(',')) endField();
        else if (c == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {}   // swallowed with the \n
        else if (c == QLatin1Char('\n')) { endField(); endRow(); }
        else { field += c; rowOpen = true; }
    }
    // A final row without newline is complete only if the output was not cut.
    if (enough) r.truncated = true;
    else if (rowOpen && !inQuotes && !incomplete) { endField(); endRow(); }
    else if (rowOpen) r.truncated = true;
    if (all.isEmpty()) { r.ok = true; r.truncated = r.truncated || incomplete; return r; }
    for (const QVariant &h : all.first()) r.columns << h.toString();
    r.totalParsed = static_cast<int>(all.size()) - 1;
    for (int i = 1; i < all.size(); ++i) {
        if (r.rows.size() >= maxRows) { r.truncated = true; break; }
        QVariantList cells = all.at(i);
        while (cells.size() < r.columns.size()) cells << QVariant();
        while (cells.size() > r.columns.size()) cells.removeLast();   // a table never has more cells than headers
        r.rows << cells;
    }
    if (incomplete) r.truncated = true;
    finishColumns(r);
    r.ok = true;
    return r;
}

DbResult parseSqliteQuote(const QByteArray &data, int maxRows, bool incomplete) {
    DbResult r;
    QStringDecoder dec(QStringDecoder::Utf8);
    const QString text = dec(data);
    const qsizetype n = text.size();
    QList<QVariantList> all;
    QVariantList row;
    qsizetype i = 0;
    bool rowOpen = false, cut = false;
    bool enough = false;   // header + maxRows rows + one more: the rest of a 4 MiB answer is never materialized
    while (i < n && !enough) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\n')) {
            if (rowOpen) { all << row; row.clear(); rowOpen = false; if (all.size() > static_cast<qsizetype>(maxRows) + 1) enough = true; }
            ++i; continue;
        }
        if (c == QLatin1Char(',')) { ++i; continue; }
        rowOpen = true;
        if (c == QLatin1Char('\'') || ((c == QLatin1Char('X') || c == QLatin1Char('x')) && i + 1 < n && text.at(i + 1) == QLatin1Char('\''))) {
            const bool blob = c != QLatin1Char('\'');
            qsizetype j = i + (blob ? 2 : 1);
            QString value;
            bool closed = false;
            while (j < n) {
                if (text.at(j) == QLatin1Char('\'')) {
                    if (j + 1 < n && text.at(j + 1) == QLatin1Char('\'') && !blob) { value += QLatin1Char('\''); j += 2; continue; }
                    closed = true; ++j; break;
                }
                value += text.at(j++);
            }
            if (!closed) { cut = true; break; }
            row << (blob ? QVariant(QStringLiteral("X'") + value + QLatin1Char('\'')) : QVariant(value));
            i = j;
        } else {
            qsizetype j = i;
            while (j < n && text.at(j) != QLatin1Char(',') && text.at(j) != QLatin1Char('\n')) ++j;
            const QString token = text.mid(i, j - i).trimmed();
            row << (token == QLatin1String("NULL") ? QVariant() : QVariant(token));
            i = j;
        }
    }
    if (enough) r.truncated = true;
    else if (rowOpen && !row.isEmpty()) { if (incomplete || cut) r.truncated = true; else all << row; }
    if (all.isEmpty()) { r.ok = true; r.truncated = r.truncated || incomplete; return r; }
    for (const QVariant &h : all.first()) r.columns << h.toString();
    r.totalParsed = static_cast<int>(all.size()) - 1;
    for (int k = 1; k < all.size(); ++k) {
        if (r.rows.size() >= maxRows) { r.truncated = true; break; }
        QVariantList cells = all.at(k);
        while (cells.size() < r.columns.size()) cells << QVariant();
        while (cells.size() > r.columns.size()) cells.removeLast();   // a table never has more cells than headers
        r.rows << cells;
    }
    if (incomplete || cut) r.truncated = true;
    finishColumns(r);
    r.ok = true;
    return r;
}

QString describeDbFailure(DbEngine engine, int exitCode, const QString &stderrText, int timeoutSec) {
    const QString err = stderrText.trimmed();
    if (err.contains(QLatin1String("FQ-ERR old-sqlite"))) return QStringLiteral("Ce sqlite3 est trop ancien pour le mode sûr (-safe, version 3.31 au moins) : par prudence, aucune requête n'est exécutée.");
    if (err.contains(QLatin1String("FQ-ERR missing-sqlite3"))) return QStringLiteral("sqlite3 n'est pas installé sur ce serveur (sudo apt install sqlite3).");
    if (err.contains(QLatin1String("FQ-ERR missing-psql"))) return QStringLiteral("psql n'est pas installé sur ce serveur (sudo apt install postgresql-client).");
    if (err.contains(QLatin1String("FQ-ERR no-file"))) return QStringLiteral("Le fichier SQLite n'existe pas sur le serveur.");
    if (err.contains(QLatin1String("FQ-ERR unreadable"))) return QStringLiteral("Le fichier SQLite n'est pas lisible par cet utilisateur.");
    if (exitCode == 124 || exitCode == 137) return QStringLiteral("Délai dépassé (%1 s) : la requête a été interrompue.").arg(timeoutSec);
    if (err.contains(QLatin1String("statement timeout"))) return QStringLiteral("Délai dépassé (%1 s) : la requête a été annulée par le serveur.").arg(timeoutSec);
    if (err.contains(QLatin1String("read-only transaction")) || err.contains(QLatin1String("readonly database")))
        return QStringLiteral("Mode lecture seule : cette requête tente d'écrire dans la base.");
    if (err.contains(QLatin1String("password authentication failed")) || err.contains(QLatin1String("no password supplied")) || err.contains(QLatin1String("authentication failed")))
        return QStringLiteral("Authentification refusée : mot de passe ou utilisateur incorrect.");
    if (err.contains(QLatin1String("Connection refused")) || err.contains(QLatin1String("could not connect")) || err.contains(QLatin1String("No such file or directory")) && engine == DbEngine::Postgres)
        return QStringLiteral("Connexion impossible au serveur PostgreSQL depuis le serveur Linux : ") + err.section(QLatin1Char('\n'), 0, 1).left(300);
    if (err.contains(QLatin1String("does not exist")) && err.contains(QLatin1String("database"))) return QStringLiteral("Cette base de données n'existe pas : ") + err.section(QLatin1Char('\n'), 0, 0).left(300);
    if (err.isEmpty()) return QStringLiteral("La commande s'est terminée avec le code %1 sans message.").arg(exitCode);
    // SQL errors: keep the tool's own wording (English), first lines only.
    return err.section(QLatin1Char('\n'), 0, 5).left(1200);
}

QList<DbObject> parseSchema(DbEngine engine, const QByteArray &output, const QString &nullMarker) {
    QList<DbObject> out;
    if (engine == DbEngine::Sqlite) {
        const DbResult r = parseSqliteQuote(output, 100000, false);
        // columns: tbl, kind, col, typ, nn
        for (const QVariantList &row : r.rows) {
            if (row.size() < 5) continue;
            const QString name = row.at(0).toString(), kind = row.at(1).toString();
            if (out.isEmpty() || out.last().name != name) out << DbObject { QString(), name, kind, {} };
            if (!row.at(2).isNull())
                out.last().columns.push_back(QVariantMap { { "name", row.at(2).toString() }, { "type", row.at(3).toString() }, { "nullable", row.at(4).toString() != QLatin1String("1") } });
        }
        return out;
    }
    const DbResult r = parsePostgresCsv(output, nullMarker, 100000, false);
    // columns: table_schema, table_name, table_type, column_name, data_type, is_nullable
    for (const QVariantList &row : r.rows) {
        if (row.size() < 6) continue;
        const QString schema = row.at(0).toString(), name = row.at(1).toString();
        if (out.isEmpty() || out.last().name != name || out.last().schema != schema)
            out << DbObject { schema, name, row.at(2).toString() == QLatin1String("VIEW") ? QStringLiteral("view") : QStringLiteral("table"), {} };
        out.last().columns.push_back(QVariantMap { { "name", row.at(3).toString() }, { "type", row.at(4).toString() }, { "nullable", row.at(5).toString() == QLatin1String("YES") } });
    }
    return out;
}

QVariantList schemaToVariant(const QList<DbObject> &objects) {
    QVariantList out;
    for (const DbObject &o : objects)
        out.push_back(QVariantMap { { "schema", o.schema }, { "name", o.name }, { "kind", o.kind }, { "columns", o.columns },
                                    { "label", o.schema.isEmpty() || o.schema == QLatin1String("public") ? o.name : o.schema + QLatin1Char('.') + o.name } });
    return out;
}

QString quoteIdentifier(const QString &name) {
    QString q = name;
    q.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString cellText(const QVariant &value, int maxChars) {
    if (value.isNull()) return QString();
    QString s = value.toString();
    if (s.size() > maxChars) s = s.left(maxChars) + QStringLiteral("…");
    return s;
}

}  // namespace fortiq
