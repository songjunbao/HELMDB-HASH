CREATE SCHEMA FVT_DBSYSTEM_MANAGE;
CREATE TABLE FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TAB_013(ID INT,ID2 INT);
COMMENT ON TABLE  FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TAB_013 IS 'PG_DESCRIPTION_TAB_013';
CREATE TABLE FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020(DESCRIPTION TEXT) INHERITS (PG_DESCRIPTION);

INSERT INTO FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 SELECT * FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
SELECT description FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;
SELECT description FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;

EXPLAIN (VERBOSE ON, COSTS OFF) UPDATE FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 SET DESCRIPTION = 'PG_DESCRIPTION_TAB_013_0' WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
UPDATE FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 SET DESCRIPTION = 'PG_DESCRIPTION_TAB_013_0' WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
SELECT description FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;
SELECT description FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;

EXPLAIN (VERBOSE ON, COSTS OFF) UPDATE PG_DESCRIPTION SET DESCRIPTION = 'PG_DESCRIPTION_TAB_013_1' WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
UPDATE PG_DESCRIPTION SET DESCRIPTION = 'PG_DESCRIPTION_TAB_013_1' WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
SELECT description FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;
SELECT description FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;

EXPLAIN (VERBOSE ON, COSTS OFF) DELETE FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
DELETE FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
SELECT description FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;
SELECT description FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;

INSERT INTO FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 SELECT * FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
EXPLAIN (VERBOSE ON, COSTS OFF) DELETE FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
DELETE FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%';
SELECT description FROM FVT_DBSYSTEM_MANAGE.PG_DESCRIPTION_TABLE_020 WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;
SELECT description FROM PG_DESCRIPTION WHERE DESCRIPTION LIKE 'PG_DESCRIPTION_TAB_013%' ORDER BY 1;

DROP SCHEMA FVT_DBSYSTEM_MANAGE CASCADE;

CREATE TABLE DELETE_XC_C(C1 INT, C2 DATE, C3 INT);
CREATE TABLE DELETE_XC_D(D1 INT, D2 DATE, D3 INT);
INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,100),NOW(), 1;
INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,100),NOW(), 2;
ALTER TABLE DELETE_XC_C ADD PRIMARY KEY (C3, C1);
INSERT INTO DELETE_XC_D SELECT * FROM DELETE_XC_C;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D2 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D2 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);

DROP TABLE DELETE_XC_C;
DROP TABLE DELETE_XC_D;

--
----/* Teset DML for replication table */
--

--
--- DELETE PREPARE
--

DROP TABLE DELETE_XC_C;
DROP TABLE DELETE_XC_D;
CREATE TABLE DELETE_XC_C(C1 INT, C2 INT, C3 INT);
CREATE TABLE DELETE_XC_D(D1 INT, D2 INT, D3 INT);

INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 0;
INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 1;
INSERT INTO DELETE_XC_D SELECT * FROM DELETE_XC_C;

--
---- DELETE CASE1: UNIQUE NOT DEFERRABLE
--
ALTER TABLE DELETE_XC_C ADD CONSTRAINT CON_DELETE UNIQUE (C3, C1);

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

ALTER TABLE DELETE_XC_C ALTER C1 SET NOT NULL;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

ALTER TABLE DELETE_XC_C ALTER C3 SET NOT NULL;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C3 = 0;
DELETE FROM DELETE_XC_C WHERE C3 = 0;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 = 2;
DELETE FROM DELETE_XC_C WHERE C2 = 2;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C;
DELETE FROM DELETE_XC_C;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

--
---- DELETE CASE2: UNIQUE DEFERRABLE
--
ALTER TABLE DELETE_XC_C DROP CONSTRAINT CON_DELETE;
ALTER TABLE DELETE_XC_C ADD CONSTRAINT CON_DELETE UNIQUE (C3, C1) DEFERRABLE;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

--
-- DELETE CASE3: PRIMARY KEY NOT DEFERRABLE
--
ALTER TABLE DELETE_XC_C DROP CONSTRAINT CON_DELETE;
ALTER TABLE DELETE_XC_C ALTER C1 DROP NOT NULL;
ALTER TABLE DELETE_XC_C ALTER C3 DROP NOT NULL;
ALTER TABLE DELETE_XC_C ADD CONSTRAINT CON_DELETE PRIMARY KEY (C3, C1);
TRUNCATE DELETE_XC_C;
INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 0;
INSERT INTO DELETE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 1;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C3 = 0;
DELETE FROM DELETE_XC_C WHERE C3 = 0;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C WHERE C2 = 2;
DELETE FROM DELETE_XC_C WHERE C2 = 2;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

SELECT * FROM DELETE_XC_C;
DELETE FROM DELETE_XC_C;
SELECT * FROM DELETE_XC_C ORDER BY 1, 2, 3;

--
-- DELETE CASE3: PRIMARY KEY DEFERRABLE
--
ALTER TABLE DELETE_XC_C DROP CONSTRAINT CON_DELETE;
ALTER TABLE DELETE_XC_C ADD CONSTRAINT CON_DELETE PRIMARY KEY (C3, C1) DEFERRABLE;

EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 IN (SELECT D3 FROM DELETE_XC_D WHERE C1 = D1 AND C2 = D2);
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C WHERE C2 = 2;
EXPLAIN (VERBOSE TRUE, COSTS FALSE)DELETE FROM DELETE_XC_C;

--
---- CLEAN UP
--
DROP TABLE DELETE_XC_C;
DROP TABLE DELETE_XC_D;

--
-- UPDATE: PREPARE
--
DROP TABLE UPDATE_XC_C;
DROP TABLE UPDATE_XC_D;
CREATE TABLE UPDATE_XC_C(C1 INT, C2 INT, C3 INT);
CREATE TABLE UPDATE_XC_D(D1 INT, D2 INT, D3 INT);

INSERT INTO UPDATE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 0;
INSERT INTO UPDATE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 1;
INSERT INTO UPDATE_XC_D SELECT * FROM UPDATE_XC_C;

--
---- UPDATE CASE1: UNIQUE NOT DEFERRABLE
--
ALTER TABLE UPDATE_XC_C ADD CONSTRAINT CON_UPDATE UNIQUE (C3, C1);

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

ALTER TABLE UPDATE_XC_C ALTER C1 SET NOT NULL;

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

ALTER TABLE UPDATE_XC_C ALTER C3 SET NOT NULL;

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

SELECT C1, C3, C2, D3 AS REPLACE_C2 FROM UPDATE_XC_C, UPDATE_XC_D WHERE C3 = D3 AND C1 = D1 ORDER BY 1, 2, 2, 4;
UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 0 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C3 = 1 AND C1 = 5 ORDER BY 1, 3, 2, 4;
UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 1 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C3 = 0 ORDER BY 1, 3, 3, 4;
UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 0 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C2 = 1 ORDER BY 1, 2, 3, 4;
UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 1 AS REPLACE_C2 FROM UPDATE_XC_C ORDER BY 1, 2, 3, 4;
UPDATE_XC_C SET C2 = 1;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

--
---- UPDATE CASE2: UNIQUE DEFERRABLE
--
ALTER TABLE UPDATE_XC_C DROP CONSTRAINT CON_UPDATE;
ALTER TABLE UPDATE_XC_C ADD CONSTRAINT CON_UPDATE UNIQUE (C3, C1) DEFERRABLE;

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

--
---- UPDATE CASE3: PRIMARY KEY NOT DEFERRABLE
--
ALTER TABLE UPDATE_XC_C DROP CONSTRAINT CON_UPDATE;
ALTER TABLE UPDATE_XC_C ALTER C1 DROP NOT NULL;
ALTER TABLE UPDATE_XC_C ALTER C3 DROP NOT NULL;
TRUNCATE UPDATE_XC_C;
INSERT INTO UPDATE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 0;
INSERT INTO UPDATE_XC_C SELECT GENERATE_SERIES(1,10), GENERATE_SERIES(1,5), 1;
ALTER TABLE UPDATE_XC_C ADD CONSTRAINT CON_UPDATE PRIMARY KEY (C3, C1);

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

SELECT C1, C3, C2, D3 AS REPLACE_C2 FROM UPDATE_XC_C, UPDATE_XC_D WHERE C3 = D3 AND C1 = D1 ORDER BY 1, 2, 2, 4;
UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 0 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C3 = 1 AND C1 = 5 ORDER BY 1, 3, 2, 4;
UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 1 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C3 = 0 ORDER BY 1, 3, 3, 4;
UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 0 AS REPLACE_C2 FROM UPDATE_XC_C WHERE C2 = 1 ORDER BY 1, 2, 3, 4;
UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;

SELECT C1, C3, C2, 1 AS REPLACE_C2 FROM UPDATE_XC_C ORDER BY 1, 2, 3, 4;
UPDATE_XC_C SET C2 = 1;
SELECT * FROM UPDATE_XC_C ORDER BY 1, 2, 3;
--
---- UPDATE CASE4: PRIMARY KEY DEFERRABLE
--
ALTER TABLE UPDATE_XC_C DROP CONSTRAINT CON_UPDATE;
ALTER TABLE UPDATE_XC_C ADD CONSTRAINT CON_UPDATE PRIMARY KEY (C3, C1) DEFERRABLE;

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET (C2) = (SELECT D3 FROM UPDATE_XC_D WHERE C3 = D3 AND C1 = D1);
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C3 = 1 AND C1 = 5;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1 WHERE C3 = 0;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 0 WHERE C2 = 1;
EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE UPDATE_XC_C SET C2 = 1;

--
---- UPDATE/DELETE REPLICATION TABLE WITH CHILD TABLE
--
CREATE TABLE parent_replica_table (A INT, B INT, C INT, D VARCHAR(20));
ALTER TABLE parent_replica_table ADD PRIMARY KEY (A, B);
INSERT INTO parent_replica_table VALUES(1, 2, 3, 'sadfadsgdsf');

CREATE TABLE son_hash_table() INHERITS (parent_replica_table);
INSERT INTO son_hash_table VALUES(1, 2, 3, 'sadfadsgdsf');
INSERT INTO son_hash_table VALUES(1, 1, 1, 'sadfadsgdsf');

EXPLAIN (VERBOSE TRUE, COSTS FALSE) DELETE FROM parent_replica_table;
DELETE FROM parent_replica_table WHERE A = 1 AND B = 1 AND C = 1;

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0 AND B > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0 AND B > 0 AND C > 0;

DROP TABLE son_hash_table;
CREATE TABLE son_replica_table() INHERITS (parent_replica_table);
ALTER TABLE son_replica_table ADD PRIMARY KEY (A, B);
INSERT INTO son_replica_table VALUES(1, 2, 3, 'sadfadsgdsf');

EXPLAIN (VERBOSE TRUE, COSTS FALSE) UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0 AND B > 0;
UPDATE parent_replica_table SET D='AAAAA' WHERE A > 0 AND B > 0 AND C > 0;

DROP TABLE son_replica_table;
DROP TABLE parent_replica_table;

--
---- CLEAN UP
--
DROP TABLE UPDATE_XC_C;
DROP TABLE UPDATE_XC_D;

--
-- create some tables
--
create table t1(val int, val2 int);
create table t2(val int, val2 int);
create table t3(val int, val2 int);

create table p1(a int, b int);
create table c1(a int, b int,d int, e int);

-- insert some rows in them
insert into t1 values(1,11),(2,11);
insert into t2 values(3,11),(4,11);
insert into t3 values(5,11),(6,11);

insert into p1 values(55,66),(77,88),(111,222),(123,345);
insert into c1 values(111,222,333,444),(123,345,567,789);

select * from t1 order by val;
select * from t2 order by val;
select * from t3 order by val;
select * from p1 order by a;
select * from c1 order by a;

-- test a few queries with row marks
select * from t1 order by 1 for update of t1 nowait;
select * from t1, t2, t3 order by 1 for update;

WITH q1 AS (SELECT * from t1 order by 1 FOR UPDATE) SELECT * FROM q1,t2 order by 1 FOR UPDATE;

WITH q1 AS (SELECT * from t1 order by 1) SELECT * FROM q1;
WITH q1 AS (SELECT * from t1 order by 1) SELECT * FROM q1 FOR UPDATE;
WITH q1 AS (SELECT * from t1 order by 1 FOR UPDATE) SELECT * FROM q1 FOR UPDATE;

-- drop objects created
drop table c1;
drop table p1;
drop table t1;
drop table t2;
drop table t3;