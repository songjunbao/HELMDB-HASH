--
-- SUBSELECT TEST
-- 子查询可以出现在目标列、FROM子句、WHERE子句、JOIN/ON子句、GROUPBY子句、HAVING子句、ORDERBY子句等位置。
--
create  table test_tbl1 (f1 numeric);
insert into test_tbl1 values (1), (1.000000000000000000001), (2), (3);

create  table test_tbl2 (f2 float8);
insert into test_tbl2 values (1), (2), (3);

select * from test_tbl2
  where f2 in (select f1 from test_tbl1) 
  ORDER BY f2;

select * from test_tbl1
  where f1 in (select f2 from test_tbl2) 
  ORDER BY f1;

DROP TABLE IF EXISTS test_table;
CREATE TABLE test_table (f1 int, f2 date);
INSERT INTO test_table VALUES(generate_series(1,10), null);

DROP TABLE IF EXISTS target_table, source_table;
CREATE TABLE target_table (c1 int, c2 varchar(200), c3 date, c4 numeric(18,9))
WITH (ORIENTATION=ROW);
CREATE TABLE source_table (c1 int, c2 varchar(200), c3 date, c4 numeric(18,9))
WITH (ORIENTATION=ROW);

INSERT INTO source_table VALUES (generate_series(11,20),'A'||(generate_series(11,20))||'Z', date'2000-03-01'+generate_series(11,20), generate_series(11,20));
INSERT INTO source_table VALUES (21, null, null, null);

-- 相关子查询
TRUNCATE target_table;
INSERT INTO target_table VALUES (generate_series(1,10),'A'||(generate_series(1,10))||'Z', date'2000-03-01'+generate_series(1,10), generate_series(1,10));

MERGE INTO target_table t
  USING source_table s ON t.c1 + (SELECT MIN(c1) + 4 FROM target_table /* 返回单行单列 */) = s.c1
WHEN MATCHED THEN
  UPDATE SET (c2, c3, c4) = (s.c2,
    (SELECT c3 FROM source_table WHERE c1 = 21 /* 返回null，select列 */),
	(SELECT c4 FROM target_table WHERE c1 = s.c1 - 10 AND c3 >= '2000-01-01') /* 带WHERE条件 */)
WHEN NOT MATCHED THEN
  INSERT VALUES (s.c1, s.c2, s.c3,
    (SELECT t2.c4 FROM test_table t1 JOIN target_table t2 ON t1.f1 = t2.c1 AND t2.c1 + 8 = s.c1) /* 多表级联 */);

SELECT c1, c2, to_char(c3, 'YYYY/MM/DD'), c4 FROM target_table ORDER BY c1;

-- 非相关子查询
TRUNCATE target_table;
INSERT INTO target_table VALUES (generate_series(1,10),'A'||(generate_series(1,10))||'Z', date'2000-03-01'+generate_series(1,10), generate_series(1,10));

MERGE INTO target_table t
  USING source_table s ON t.c1 + (SELECT MIN(f1) + 4 FROM test_table /* 返回单行单列 */) = s.c1
WHEN MATCHED THEN
  UPDATE SET (c2, c3, c4) = (s.c2,
    (SELECT f2 FROM test_table WHERE f1 = 1 /* 返回null，select列 */),
	(SELECT f1 FROM test_table WHERE f1 > 7 AND f1 < 9) /* 带WHERE条件 */)
WHEN NOT MATCHED THEN
  INSERT VALUES (s.c1, s.c2, s.c3,
    (SELECT t2.f1 + t2.f1 FROM test_table t1 JOIN test_table t2 ON t1.f1 = t2.f1 AND t1.f1 < 2 ) /* 多表级联 */);

SELECT c1, c2, to_char(c3, 'YYYY/MM/DD'), c4 FROM target_table ORDER BY c1;

-- 子查询嵌套
TRUNCATE target_table;
INSERT INTO target_table VALUES (generate_series(1,10),'A'||(generate_series(1,10))||'Z', date'2000-03-01'+generate_series(1,10), generate_series(1,10));

MERGE INTO target_table t
  USING source_table s ON t.c1 + (SELECT MIN(c1) + (SELECT f1 FROM test_table WHERE f1 > 3 AND f1 <= 4) FROM target_table /* SELECT列嵌套 */) = s.c1
WHEN MATCHED THEN
  UPDATE SET (c2, c3, c4) = (s.c2,
    (SELECT c3 FROM source_table WHERE c1 = 21),
	(SELECT c4 FROM target_table WHERE c1 = s.c1 - (SELECT MAX(f1) FROM test_table) AND c3 >= '2000-01-01') /* WHERE条件嵌套 */)
WHEN NOT MATCHED THEN
  INSERT VALUES (s.c1, s.c2, s.c3,
    (SELECT t2.c4 FROM test_table t1 JOIN target_table t2 ON t1.f1 = t2.c1 AND t2.c1 + 8 = s.c1) /* 多表级联 */);

SELECT c1, c2, to_char(c3, 'YYYY/MM/DD'), c4 FROM target_table ORDER BY c1;

DROP TABLE IF EXISTS target_table, source_table, test_tbl1, test_tbl2;

-- test optimize subselect with materialize 
create table mat_subselect_1(c1 int, c2 int);
insert into mat_subselect_1 values (generate_series(1, 1000), 666);

create table mat_subselect_2(d1 int, d2 int);
insert into mat_subselect_2 values(generate_series(300, 500), 222);
insert into mat_subselect_2 values (generate_series(1000, 20000), 1);

set enable_seqscan to off;
explain(costs off) select * from mat_subselect_1 
    where c1 in (
      select d1 from mat_subselect_2 where d1 < 340 and d1 > 330
      ) and c2 != 0;
select * from mat_subselect_1 where c1 in (
    select d1 from mat_subselect_2 where d1 < 340 and d1 > 330
  ) and c2 != 0;
reset enable_seqscan;
drop table mat_subselect_1, mat_subselect_2;
