#pragma once

#include <QByteArray>

#include "../core/SecureWipe.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// Bases de données (read-only): SQLite files and PostgreSQL servers queried with
// the existing `sqlite3` and `psql` clients on the selected Linux server, over
// SSH. SQL and passwords travel as data inside a `sh -s` script on stdin, never
// as command arguments. Read-only is enforced in layers (this scanner, the
// clients' own options, the server session); it is a guard against accidents,
// NOT a security boundary: use a database role without write rights.
// See docs/DATABASES.md.

enum class DbEngine { Sqlite, Postgres };

struct DbProfile {
    QString id;
    QString serverId;     // the SSH server the client runs on (profiles belong to one server)
    QString name;
    DbEngine engine = DbEngine::Sqlite;
    QString path;         // sqlite: absolute file path on the server
    QString host;         // postgres: empty = local socket
    int port = 5432;
    QString user;
    QString database;
    bool hasPassword = false;   // the secret itself is in the protected store, never here
};

// Validates a profile; returns an empty string when valid, otherwise the reason.
QString validateProfile(const DbProfile &profile);
QVariantMap profileToVariant(const DbProfile &profile);
DbProfile profileFromVariant(const QVariantMap &map);
QString engineName(DbEngine engine);   // "sqlite" | "postgres"

// ---- SQL scanning (before anything is sent) ----
struct SqlScan {
    int statements = 0;              // non-empty statements separated by ';' outside quotes/comments
    bool unterminated = false;       // open string, identifier, comment or dollar quote
    bool metaCommand = false;        // psql '\' outside quotes, or an sqlite3 '.' command at a line start
    QString firstKeyword;            // lower-case, "" when empty
    QStringList words;               // identifiers and keywords outside strings and comments, lower-case (function names among them)
};
SqlScan scanSql(const QString &sql, DbEngine engine);

// Message for the user when the SQL must not be sent in read-only mode; empty when it may be sent.
QString readOnlyRefusal(const QString &sql, DbEngine engine);

// ---- limits ----
struct DbLimits {
    int timeoutSec = 15;       // 1..120
    int maxRows = 500;         // 1..5000 shown
};
DbLimits clampLimits(DbLimits in);
constexpr qint64 kMaxDbOutput = 4 * 1024 * 1024;   // stdout cap of one run

// ---- scripts ----
QString randomToken(int bytes = 12);   // hex, for heredoc delimiters and the NULL marker
// `delimiter` must not occur in the SQL (the builder checks and returns an empty script otherwise).
QByteArray buildSqliteScript(const DbProfile &p, const QString &sql, const DbLimits &limits, const QString &delimiter);
SecretBytes buildPostgresScript(const DbProfile &p, const QString &password, const QString &sql, const DbLimits &limits,
                               const QString &nullMarker, const QString &delimiter);
// Fixed catalogue queries (not user SQL) for the schema tree.
SecretBytes buildSchemaScript(const DbProfile &p, const QString &password, const DbLimits &limits, const QString &nullMarker, const QString &delimiter);

// ---- what the connected role can do (audit 23.10.2026, R4) ----
// FORTIQ's read-only mode is a guard against accidents. The real guarantee is a database role that cannot write, so every time a
// profile is opened FORTIQ reads what the role is allowed to do (a fixed catalogue query, nothing the user typed) and says it.
struct RoleCheck {
    bool ran = false;            // the check was executed and understood
    QString level;               // "ok" | "note" | "warning" | "danger" | "unknown"
    QString summary;             // one line for the banner
    QStringList findings;        // one line per right found
    QString recommendation;      // what to do (for PostgreSQL: a read-only role recipe, never executed by FORTIQ)
};
SecretBytes buildRoleCheckScript(const DbProfile &profile, const QString &password, const DbLimits &limits, const QString &nullMarker, const QString &delimiter);
RoleCheck assessRoleCheck(const DbProfile &profile, const QByteArray &output, const QString &nullMarker);
QString readOnlyRoleRecipe(const DbProfile &profile);       // SQL text for an administrator; contains a placeholder password
QVariantMap roleCheckToVariant(const RoleCheck &check);

// ---- results ----
struct DbColumn { QString name; bool numeric = false; };
struct DbResult {
    bool ok = false;
    QString error;                   // user-facing message
    QStringList columns;
    QList<QVariantList> rows;        // null cell = QVariant()
    QList<bool> numeric;             // per column: every non-null value looks like a number
    bool truncated = false;          // more rows existed than maxRows / the output cap was hit
    int totalParsed = 0;
};
// PostgreSQL --csv output with a NULL marker. `incomplete` = the output was cut by the byte cap.
DbResult parsePostgresCsv(const QByteArray &data, const QString &nullMarker, int maxRows, bool incomplete);
// sqlite3 `.mode quote` + `.headers on` output.
DbResult parseSqliteQuote(const QByteArray &data, int maxRows, bool incomplete);

// Turns the process outcome into a result/error. `exitCode` is the shell/tool exit status.
QString describeDbFailure(DbEngine engine, int exitCode, const QString &stderrText, int timeoutSec);

// Schema tree from the output of buildSchemaScript.
struct DbObject { QString schema, name, kind; QVariantList columns; };
QList<DbObject> parseSchema(DbEngine engine, const QByteArray &output, const QString &nullMarker);
QVariantList schemaToVariant(const QList<DbObject> &objects);

// Identifier quoting for generated "SELECT * FROM ..." (identifiers come from the database: never trusted).
QString quoteIdentifier(const QString &name);

QString cellText(const QVariant &value, int maxChars = 400);

}  // namespace fortiq
