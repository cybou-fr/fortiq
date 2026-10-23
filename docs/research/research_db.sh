#!/bin/bash
export LC_ALL=C.UTF-8 LANG=C.UTF-8
echo "=== postgres cluster"
pg_ctlcluster 17 main start 2>&1 | head -2
sleep 3
su postgres -c "psql -X -At -c \"select version()\"" 2>&1 | head -1
su postgres -c "psql -X -q -c \"create role fqdb login password 'fqdb-test-pw'\"" 2>&1 | head -2
su postgres -c "psql -X -q -c \"create database fqtest owner fqdb\"" 2>&1 | head -2
PGPASSWORD=fqdb-test-pw psql -X -q -h 127.0.0.1 -U fqdb -d fqtest -v ON_ERROR_STOP=1 <<'SQL' 2>&1 | head -5
create table people(id serial primary key, name text, note text, born date, score numeric(6,2), data jsonb);
insert into people(name,note,born,score,data) values
 ('Zoë Müller','café "quoted", comma;semi',  '1990-05-01', 12.50, '{"a":1,"b":[1,2]}'),
 ('Иван', E'line1\nline2\ttab', '1985-12-31', NULL, NULL),
 ('NULL-test', NULL, NULL, 0, '{}'::jsonb);
create table big as select g as n, md5(g::text) as h, repeat('x', 200) as pad from generate_series(1,20000) g;
create view v_people as select id,name from people;
SQL
echo "=== psql modes (as fqdb)"
export PGPASSWORD=fqdb-test-pw PGCONNECT_TIMEOUT=5
P="psql -X -h 127.0.0.1 -U fqdb -d fqtest"
echo "-- csv:"; $P --csv -c "select id,name,note,born,score from people order by id"
echo "-- tuples-only unaligned tab:"; $P -At -F $'\t' -c "select id,name,score from people order by id"
echo "-- json via json_agg:"; $P -At -c "select json_agg(t) from (select * from people order by id) t"
echo "-- null vs empty in csv:"; $P --csv -c "select NULL as n, '' as e"
echo "-- row cap by wrapper:"; $P -At -c "select count(*) from (select * from big limit 1000) t"
echo "-- error (syntax):"; $P -c "selct 1"; echo "exit=$?"
echo "-- error (no table) with ON_ERROR_STOP:"; $P -v ON_ERROR_STOP=1 -c "select * from nope"; echo "exit=$?"
echo "-- wrong password:"; PGPASSWORD=bad $P -c "select 1"; echo "exit=$?"
echo "-- connection refused:"; psql -X -h 127.0.0.1 -p 59999 -U x -d y -c "select 1"; echo "exit=$?"
echo "-- read-only session:"; $P -c "set default_transaction_read_only = on; create table t2(x int)"; echo "exit=$?"
echo "-- read-only via PGOPTIONS:"; PGOPTIONS='-c default_transaction_read_only=on -c statement_timeout=5000' $P -c "create table t3(x int)"; echo "exit=$?"
echo "-- statement timeout:"; PGOPTIONS='-c statement_timeout=500' $P -c "select pg_sleep(2)"; echo "exit=$?"
echo "-- list databases:"; $P -At -c "select datname from pg_database where not datistemplate order by 1"
echo "-- schema introspection:"; $P --csv -c "select table_name, table_type from information_schema.tables where table_schema='public' order by 1"
echo "-- columns:"; $P --csv -c "select column_name,data_type,is_nullable from information_schema.columns where table_name='people' order by ordinal_position"
echo "-- big output size (20000 rows):"; $P --csv -c "select * from big" | wc -c
echo "-- ssl info:"; $P -At -c "show ssl"
echo "-- processlist-ish:"; $P -At -c "select count(*) from pg_stat_activity"

echo "=== sqlite3"
rm -f /tmp/fq.db
sqlite3 /tmp/fq.db "create table people(id integer primary key, name text, note text, score real, b blob); insert into people(name,note,score,b) values ('Zoë','a,b\"c',1.5,x'00ff10'),('Иван',NULL,NULL,NULL);"
echo "-- json:"; sqlite3 -json /tmp/fq.db "select * from people"
echo "-- csv header:"; sqlite3 -header -csv /tmp/fq.db "select id,name,note,score from people"
echo "-- readonly open + write attempt:"; sqlite3 -readonly /tmp/fq.db "insert into people(name) values('x')"; echo "exit=$?"
echo "-- query_only pragma:"; sqlite3 /tmp/fq.db "pragma query_only=on; insert into people(name) values('x')"; echo "exit=$?"
echo "-- syntax error:"; sqlite3 /tmp/fq.db "selct"; echo "exit=$?"
echo "-- missing file readonly:"; sqlite3 -readonly /tmp/nope.db "select 1"; echo "exit=$?"
echo "-- schema:"; sqlite3 -json /tmp/fq.db "select name,type from sqlite_master order by 1"
echo "-- columns:"; sqlite3 -json /tmp/fq.db "pragma table_info(people)"
echo "-- not a db:"; echo "garbage" > /tmp/notdb.db; sqlite3 -readonly /tmp/notdb.db "select 1"; echo "exit=$?"
echo "-- timeout/bail:"; sqlite3 -bail -cmd ".timeout 1000" /tmp/fq.db "select count(*) from people"

echo "=== mariadb client flags"
mariadb --help 2>&1 | grep -iE "json|batch|raw|skip-column|connect-timeout|defaults-extra|ssl-verify|ssl " | head -12
echo "-- no server: connection error format:"; mariadb -h 127.0.0.1 -P 59999 -u x --connect-timeout=3 -e "select 1" 2>&1; echo "exit=$?"
