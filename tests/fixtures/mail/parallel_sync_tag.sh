#!/bin/bash
# Audit R9: `notmuch tag` renames the files of a Maildir (flag letters in the name) while `mbsync` reads and writes the same
# Maildir. This script runs both at the same time, many times, and prints one KEY=value line per fact; the live test judges them.
# It uses a private Maildir "far" as the mail source (no IMAP server needed) and a private Maildir "near" that notmuch indexes,
# all below ~/par-test of the test user. NEVER run it on a real mailbox.
#   parallel_sync_tag.sh [rounds, default 8] [new messages per round, default 15] [pause between tag operations, default 1.5 s]
# Finding that shaped it: with NO pause (tags renamed in a tight loop) mbsync 1.5 never finishes ("sleeping due to recent directory
# modification"), it waits for the directory to be quiet; so a pause is needed for it to complete, and the app must not tag in bursts while syncing.
set -u
ROUNDS=${1:-8}
PER=${2:-15}
PAUSE=${3:-1.5}      # seconds between two tag operations while mbsync runs
W=$HOME/par-test
rm -rf "$W"
mkdir -p "$W/far/cur" "$W/far/new" "$W/far/tmp" "$W/near"

cat > "$W/mbsyncrc" <<EOF
MaildirStore far
Path $W/far/
Inbox $W/far/

MaildirStore near
Path $W/near/
Inbox $W/near/
SubFolders Verbatim

Channel par
Far :far:
Near :near:
Patterns INBOX
Create Near
Expunge None
Sync Pull
SyncState *
EOF

cat > "$W/nm.conf" <<EOF
[database]
path=$W/near
[user]
name=Par Test
primary_email=par@example.org
[new]
tags=unread;inbox
ignore=.uidvalidity;.mbsyncstate;.mbsyncstate.new;.mbsyncstate.journal;.mbsyncstate.lock
[maildir]
synchronize_flags=true
EOF
export NOTMUCH_CONFIG="$W/nm.conf"

n=0
deliver() {   # $1 = how many
    local i
    for i in $(seq 1 "$1"); do
        n=$((n + 1))
        printf 'From: a@example.org\nTo: b@example.org\nSubject: message %d\nMessage-ID: <par-%d@example.org>\nDate: Thu, 22 Oct 2026 09:%02d:00 +0000\n\nbody %d\n' \
            "$n" "$n" $((n % 60)) "$n" > "$W/far/new/par-$n.eml"
    done
}

notmuch new >/dev/null 2>&1
deliver "$PER"
mbsync -c "$W/mbsyncrc" par >"$W/first.out" 2>&1 || echo "FIRST_SYNC_FAILED=$?"
notmuch new >/dev/null 2>&1

fails=0; failtext=""; tagops=0; syncs=0; slowest=0
for r in $(seq 1 "$ROUNDS"); do
    deliver "$PER"
    started=$(date +%s)
    timeout 120 mbsync -c "$W/mbsyncrc" par >"$W/sync-$r.out" 2>&1 &
    pid=$!
    # While mbsync runs: index, then flip tags that rename files (unread <-> read, flagged), like a user reading and flagging,
    # with a pause between the operations (mbsync waits until the directory has been quiet for a moment).
    while kill -0 "$pid" 2>/dev/null; do
        notmuch new >/dev/null 2>&1
        notmuch tag -unread '*' >/dev/null 2>&1
        notmuch tag +flagged tag:inbox >/dev/null 2>&1
        notmuch tag +unread -flagged tag:inbox >/dev/null 2>&1
        tagops=$((tagops + 3))
        sleep "$PAUSE"
    done
    wait "$pid"; rc=$?
    took=$(( $(date +%s) - started )); [ "$took" -gt "$slowest" ] && slowest=$took
    syncs=$((syncs + 1))
    if [ "$rc" != 0 ]; then
        fails=$((fails + 1))
        failtext="$failtext [round $r rc=$rc] $(tr '\n' ' ' < "$W/sync-$r.out" | cut -c1-300)"
    fi
done

# Quiet pass: whatever the races did, one clean run must converge and lose nothing.
mbsync -c "$W/mbsyncrc" par >"$W/final.out" 2>&1; final_rc=$?
notmuch new >/dev/null 2>&1
notmuch tag +unread -flagged tag:inbox >/dev/null 2>&1

total=$n
far_files=$(find "$W/far" -type f \( -path '*/cur/*' -o -path '*/new/*' \) | wc -l)
near_files=$(find "$W/near" -type f \( -path '*/cur/*' -o -path '*/new/*' \) | wc -l)
indexed=$(notmuch count '*' 2>/dev/null)
dups=$(notmuch search --output=messages '*' 2>/dev/null | sort | uniq -d | wc -l)
unique_ids=$(notmuch search --output=messages '*' 2>/dev/null | sort -u | wc -l)
tmp_left=$(find "$W/near" -type f -path '*/tmp/*' | wc -l)

echo "TOTAL=$total"
echo "FAR_FILES=$far_files"
echo "NEAR_FILES=$near_files"
echo "INDEXED=$indexed"
echo "UNIQUE_IDS=$unique_ids"
echo "DUPLICATES=$dups"
echo "TMP_LEFT=$tmp_left"
echo "SYNCS=$syncs"
echo "SYNC_FAILURES=$fails"
echo "SYNC_FAILURE_TEXT=$failtext"
echo "FINAL_SYNC_RC=$final_rc"
echo "TAG_OPS=$tagops"
echo "SLOWEST_SYNC_SECONDS=$slowest"
echo "FINAL_SYNC_OUT=$(tr '\n' ' ' < "$W/final.out" | cut -c1-300)"
