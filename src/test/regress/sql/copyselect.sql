--
-- Test cases for COPY (select) TO
--
create table test1 (id int, t text);
insert into test1 values (1, 'a');
insert into test1 values (2, 'b');
insert into test1 values (3, 'c');
insert into test1 values (4, 'd');
insert into test1 values (5, 'e');

create table test2 (id int, t text);
insert into test2 values (1, 'A');
insert into test2 values (2, 'B');
insert into test2 values (3, 'C');
insert into test2 values (4, 'D');
insert into test2 values (5, 'E');

create view v_test1
as select 'v_'||t from test1;

--
-- Test COPY table TO
--
copy (select * from test1 order by 1) to stdout;
--
-- This should fail
--
copy v_test1 to stdout;
--
-- Test COPY (select) TO
--
copy (select t from test1 where id=1) to stdout;
--
-- Test COPY (select for update) TO
--
copy (select t from test1 where id=3 for update) to stdout;
--
-- This should fail
--
copy (select t into temp test3 from test1 where id=3) to stdout;
--
-- This should fail
--
copy (select * from test1) from stdin;
--
-- This should fail
--
copy (select * from test1) (t,id) to stdout;
--
-- Test JOIN
--
copy (select * from test1 join test2 using (id) order by 1) to stdout;
--
-- Test UNION SELECT
--
copy (select t from test1 where id = 1 UNION select * from v_test1 ORDER BY 1) to stdout;
--
-- Test subselect
--
copy (select * from (select t from test1 where id = 1 UNION select * from v_test1 ORDER BY 1) t1 order by 1) to stdout;
--
-- Test headers, CSV and quotes
--
copy (select t from test1 where id = 1) to stdout csv header force quote t;
--
-- Test psql builtins, plain table
--
\copy (select * from test1 order by 1) to stdout
--
-- This should fail
--
\copy v_test1 to stdout
--
-- Test \copy (select ...)
--
\copy (select "id",'id','id""'||t,(id + 1)*id,t,"test1"."t" from test1 where id=3) to stdout
--
-- Drop everything
--
drop table test2;
drop view v_test1;
drop table test1;

-- psql handling of COPY in multi-command strings
copy (select 1) to stdout\; select 1/0;	-- row, then error
select 1/0\; copy (select 1) to stdout; -- error only
copy (select 1) to stdout\; copy (select 2) to stdout\; select 0\; select 3; -- 1 2 3

create table test3 (c int);
select 0\; copy test3 from stdin\; copy test3 from stdin\; select 1; -- 1
1
\.
2
\.
select * from test3 order by 1;

set query_dop=1004;
copy (select * from test3 order by 1 limit 1) to stdout header csv;
drop table test3;
