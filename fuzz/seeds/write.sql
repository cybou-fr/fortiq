with x as (select 1) delete from t; drop table t; select writefile('/tmp/x', 'y'); .shell id
\! id