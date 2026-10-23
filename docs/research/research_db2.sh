#!/bin/bash
export LC_ALL=C.UTF-8 LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive
export PGPASSWORD=fqdb-test-pw PGCONNECT_TIMEOUT=5
P="psql -X -h 127.0.0.1 -U fqdb -d fqtest"
echo "=== psql NULL marker in csv"
$P --csv -P null='<<N-7f3a>>' -c "select NULL as n, '' as e, 'x' as v"
echo "=== psql unaligned with separators (names + types)"
$P -A -P null='<<N-7f3a>>' -F $'\x1f' -R $'\x1e' -c "select 1 as a, NULL as b, 'x' as c" | od -c | head -4
echo "=== psql multiple statements in one -c / stdin script"
$P --csv -P null='<<N>>' <<'SQL'
select 1 as a;
select 'two' as b, NULL as c;
SQL
echo "=== psql read-only start option still blocks write on stdin script"
PGOPTIONS='-c default_transaction_read_only=on' $P -c "insert into people(name) values('x')"; echo "exit=$?"
echo "=== psql user can disable the guard (documented limitation)"
PGOPTIONS='-c default_transaction_read_only=on' $P -c "set default_transaction_read_only=off; select 1"; echo "exit=$?"
echo "=== psql BEGIN READ ONLY wrapper"
$P -c "begin read only; create table t9(x int); commit" 2>&1 | head -3
echo "=== binary and special values"
$P --csv -P null='<<N>>' -c "select '\\x00ff'::bytea as b, 'a'::char(3) as c, 1.5::float8 as f, now()::date is not null as d, array[1,2] as arr, '{\"k\":1}'::json as j"
echo "=== psql version flags"
psql --version; $P -At -c "show server_version"

echo "=== install mariadb-server (disposable) and test --xml/--batch"
apt-get install -y --no-install-recommends mariadb-server >/tmp/apt2.log 2>&1; echo apt-exit=$?
mkdir -p /run/mysqld; chown mysql:mysql /run/mysqld
(nohup mysqld_safe --skip-networking=0 --bind-address=127.0.0.1 >/tmp/mysqld.log 2>&1 &)
for i in 1 2 3 4 5 6 7 8 9 10; do mariadb -uroot -e "select 1" >/dev/null 2>&1 && break; sleep 2; done
mariadb -uroot -e "create database if not exists fqtest; create user if not exists 'fqdb'@'127.0.0.1' identified by 'fqdb-test-pw'; create user if not exists 'fqdb'@'localhost' identified by 'fqdb-test-pw'; grant all on fqtest.* to 'fqdb'@'127.0.0.1'; grant all on fqtest.* to 'fqdb'@'localhost';" 2>&1 | head -3
mariadb -uroot fqtest -e "create table if not exists people(id int primary key auto_increment, name text, note text, score decimal(6,2)); delete from people; insert into people(name,note,score) values ('Zoë Müller','café \"q\", c;s',12.5),('Иван',NULL,NULL),('NULL',  'NULL', 0),('tab\there', 'nl\nhere','1');" 2>&1 | head -3
export MYSQL_PWD=fqdb-test-pw
M="mariadb -h 127.0.0.1 -u fqdb fqtest --connect-timeout=5 --skip-ssl"
echo "-- batch raw (NULL ambiguity):"; $M --batch --raw -e "select * from people" 2>&1
echo "-- batch (escaped):"; $M --batch -e "select * from people" 2>&1
echo "-- xml:"; $M --xml -e "select * from people" 2>&1
echo "-- error:"; $M --batch -e "selct" 2>&1; echo "exit=$?"
echo "-- wrong password:"; MYSQL_PWD=bad $M -e "select 1" 2>&1; echo "exit=$?"
echo "-- read only guard:"; $M --init-command="SET SESSION TRANSACTION READ ONLY" -e "insert into people(name) values('x')" 2>&1; echo "exit=$?"
echo "-- schema:"; $M --batch -e "select table_name,table_type from information_schema.tables where table_schema='fqtest'" 2>&1
echo "-- max rows / timeout:"; $M --batch -e "set statement max_statement_time=0.2 for select sleep(2)" 2>&1; echo "exit=$?"
