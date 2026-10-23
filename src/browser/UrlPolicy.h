#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

namespace fortiq {

// Address policy of the Browser (audit 23.10.2026, R1). The Browser asks the Linux SERVER to fetch pages, so the
// addresses it can reach are the server's own: its loopback, its private network and, in clouds, the metadata service
// that hands out credentials. The rules, applied to the addresses the server RESOLVED (never to the typed text):
//   - public address                       -> fetched;
//   - internal address (loopback, private, CGNAT, ULA, reserved)  -> only after the user confirms, once, for that
//                                              host and port during that navigation (redirects included);
//   - blocked address (cloud metadata, link-local, multicast, unspecified, broadcast) -> never fetched, no way around.
// A name with a mix of public and internal addresses is internal (the usual DNS rebinding shape).

enum class AddressClass { Public, Internal, Blocked };

struct AddressVerdict {
    AddressClass cls = AddressClass::Blocked;
    QString label;     // short French reason: "boucle locale", "réseau privé", "métadonnées du cloud" ...
};

// Classifies one numeric address (IPv4, IPv6, IPv4-mapped / NAT64 / 6to4 are judged by the IPv4 inside). Unreadable -> Blocked.
AddressVerdict classifyAddress(const QString &ip);

// What may be done with a request to `host:port` given its resolved addresses.
struct PolicyDecision {
    enum class Kind { Allow, Confirm, Block } kind = Kind::Block;
    QString reason;            // user-facing text (Confirm: why it asks; Block: why it refuses)
    QString pinIp;             // the address curl must be pinned to (Allow / Confirm); empty when blocked
    QVariantList details;      // [{ip, label, cls: "public"|"internal"|"blocked"}]
};
PolicyDecision decideAddresses(const QStringList &resolvedIps);

// Host rules (before anything is resolved): only canonical names and canonical IP literals are accepted, so that curl
// and the resolver cannot read the same text as different addresses (decimal / octal / hex / short IPv4 forms are refused).
struct HostPlan {
    bool ok = false;
    QString error;
    QString host;              // lower-case host without brackets
    int port = 0;              // explicit or default (80 / 443)
    bool literalIp = false;    // an IP literal: nothing to resolve, the address is classified as written
    QString ip;                // the literal (canonical text) when literalIp
    bool blockedName = false;  // a well-known metadata host name
};
HostPlan planHost(const QString &normalizedUrl);

// ---- resolving on the server ----
// Prints one numeric address per line (`getent ahosts`), at most 8. The host is data (single-quoted).
QByteArray buildResolveScript(const QString &host);
// Parses that output; keeps valid addresses in order, drops duplicates and zone ids.
QStringList parseResolveOutput(const QByteArray &output, QString *problem);

// "host:port:ip" for curl --resolve (IPv6 in brackets). Empty for literal IPs.
QString curlResolveSpec(const QString &host, int port, const QString &ip);

}  // namespace fortiq
