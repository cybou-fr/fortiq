#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the database role-check live test: roles with very
# different rights on the test database `fqtest` (the owner role `fqdb` and its tables come from the research setup).
#   fqro     SELECT only: the role FORTIQ recommends                            -> level ok
#   fqfile   SELECT only + pg_read_server_files                                 -> level warning
#   fqwfile  SELECT only + pg_write_server_files                                -> level danger
#   fqsu     SUPERUSER                                                          -> level danger
# Run as root. Passwords are throw-away test values.
set -e
export LC_ALL=C.UTF-8
psqlr() { su postgres -c "psql -v ON_ERROR_STOP=1 -q -d fqtest -c \"$1\""; }
for r in fqro fqfile fqwfile fqsu; do
  psqlr "drop owned by $r" 2>/dev/null || true
  psqlr "drop role if exists $r"
done
psqlr "create role fqro login password 'fqro-test-pw'"
psqlr "create role fqfile login password 'fqfile-test-pw' in role pg_read_server_files"
psqlr "create role fqwfile login password 'fqwfile-test-pw' in role pg_write_server_files"
psqlr "create role fqsu login superuser password 'fqsu-test-pw'"
for r in fqro fqfile fqwfile; do
  psqlr "grant connect on database fqtest to $r"
  psqlr "grant usage on schema public to $r"
  psqlr "grant select on all tables in schema public to $r"
done
psqlr "revoke create on schema public from public"
for r in fqro fqfile fqwfile fqsu; do
  PGPASSWORD="$r-test-pw" psql -h 127.0.0.1 -U "$r" -d fqtest -tAc "select current_user" | tr '\n' ' '
done
echo
echo ready
