#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace fortiq::engine {

// Configuration of the Browser engine (Qt WebEngine = Chromium; docs/BROWSER.md, "Moteur"). This file is PURE (Qt Core only): the same
// rules are applied by the application and checked by the offline tests, so a Qt update that renames a switch cannot silently
// reopen a way around the SSH gateway. The goal: the engine has exactly ONE way out, the loopback gateway of the selected server.

// Chromium switches, one per entry ("--name=value"), for a gateway listening on 127.0.0.1:`proxyPort`.
// Applied BEFORE the first profile is created, through QTWEBENGINE_CHROMIUM_FLAGS (applyChromiumFlags).
QStringList chromiumSwitches(quint16 proxyPort);

// The names passed to --disable-features (also reachable through chromiumSwitches()).
QStringList disabledFeatures();

// Puts the switches in the environment (QTWEBENGINE_CHROMIUM_FLAGS). Returns false when the variable already holds something the
// user or a script set: foreign switches could reopen a direct route, so the engine is then refused instead of started.
bool applyChromiumFlags(quint16 proxyPort, QString *problem = nullptr);

// Reads a switch list back (the command line of a running QtWebEngineProcess, or the environment) and reports every mandatory
// rule that is missing or contradicted. Empty list = the configuration is complete.
QStringList auditSwitches(const QStringList &switches, quint16 proxyPort);

// Navigation policy: only http(s) pages are shown, plus about:blank. Everything else (file:, ftp:, chrome:, devtools:, view-source:,
// javascript: addresses, qrc:, data:/blob: typed by the user) is refused with a French reason.
struct NavigationVerdict {
    bool allowed = false;
    QString reason;   // user-facing when refused
};
// `topLevel` = the page itself is navigated; sub-resources are judged with the same list plus data: and blob: that pages build.
NavigationVerdict judgeUrl(const QUrl &url, bool topLevel);

// The proxy credentials may only ever be given to OUR gateway.
bool isOurProxy(const QString &host, int port, quint16 gatewayPort);

// Permissions a page can ask for (geolocation, camera, notifications, clipboard...) are all refused.
QString permissionRefusal();

}  // namespace fortiq::engine
