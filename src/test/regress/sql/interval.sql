--
-- INTERVAL
--

SET DATESTYLE = 'ISO';
SET IntervalStyle to postgres;

-- check acceptance of "time zone style"
SELECT INTERVAL '01:00' AS "One hour";
SELECT INTERVAL '+02:00' AS "Two hours";
SELECT INTERVAL '-08:00' AS "Eight hours";
SELECT INTERVAL '-1 +02:03' AS "22 hours ago...";
SELECT INTERVAL '-1 days +02:03' AS "22 hours ago...";
SELECT INTERVAL '1.5 weeks' AS "Ten days twelve hours";
SELECT INTERVAL '1.5 months' AS "One month 15 days";
SELECT INTERVAL '10 years -11 month -12 days +13:14' AS "9 years...";
--check interval concat null
SELECT '' || INTERVAL '10 years -11 month -12 days +13:14' AS "9 years...";

CREATE TABLE INTERVAL_TBL (f1 interval);

INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 1 minute');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 5 hour');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 10 day');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 34 year');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 3 months');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 14 seconds ago');
INSERT INTO INTERVAL_TBL (f1) VALUES ('1 day 2 hours 3 minutes 4 seconds');
INSERT INTO INTERVAL_TBL (f1) VALUES ('6 years');
INSERT INTO INTERVAL_TBL (f1) VALUES ('5 months');
INSERT INTO INTERVAL_TBL (f1) VALUES ('5 months 12 hours');

-- badly formatted interval
INSERT INTO INTERVAL_TBL (f1) VALUES ('badly formatted interval');
INSERT INTO INTERVAL_TBL (f1) VALUES ('@ 30 eons ago');

-- test interval operators

SELECT '' AS ten, * FROM INTERVAL_TBL ORDER BY f1;

SELECT '' AS nine, * FROM INTERVAL_TBL
   WHERE INTERVAL_TBL.f1 <> interval '@ 10 days' ORDER BY f1;

SELECT '' AS three, * FROM INTERVAL_TBL
   WHERE INTERVAL_TBL.f1 <= interval '@ 5 hours' ORDER BY f1;

SELECT '' AS three, * FROM INTERVAL_TBL
   WHERE INTERVAL_TBL.f1 < interval '@ 1 day' ORDER BY f1;

SELECT '' AS one, * FROM INTERVAL_TBL
   WHERE INTERVAL_TBL.f1 = interval '@ 34 years' ORDER BY f1;

SELECT '' AS five, * FROM INTERVAL_TBL 
   WHERE INTERVAL_TBL.f1 >= interval '@ 1 month' ORDER BY f1;

SELECT '' AS nine, * FROM INTERVAL_TBL
   WHERE INTERVAL_TBL.f1 > interval '@ 3 seconds ago' ORDER BY f1;

SELECT '' AS fortyfive, r1.*, r2.*
   FROM INTERVAL_TBL r1, INTERVAL_TBL r2
   WHERE r1.f1 > r2.f1
   ORDER BY r1.f1, r2.f1;


-- Test multiplication and division with intervals.
-- Floating point arithmetic rounding errors can lead to unexpected results,
-- though the code attempts to do the right thing and round up to days and
-- minutes to avoid results such as '3 days 24:00 hours' or '14:20:60'.
-- Note that it is expected for some day components to be greater than 29 and
-- some time components be greater than 23:59:59 due to how intervals are
-- stored internally.

CREATE TABLE INTERVAL_MULDIV_TBL (span interval);
COPY INTERVAL_MULDIV_TBL FROM STDIN;
41 mon 12 days 360:00
-41 mon -12 days +360:00
-12 days
9 mon -27 days 12:34:56
-3 years 482 days 76:54:32.189
4 mon
14 mon
999 mon 999 days
\.

SELECT span * 0.3 AS product
FROM INTERVAL_MULDIV_TBL ORDER BY span;

SELECT span * 8.2 AS product
FROM INTERVAL_MULDIV_TBL ORDER BY span;

SELECT span / 10 AS quotient
FROM INTERVAL_MULDIV_TBL ORDER BY span;

SELECT span / 100 AS quotient
FROM INTERVAL_MULDIV_TBL ORDER BY span;

DROP TABLE INTERVAL_MULDIV_TBL;

SET DATESTYLE = 'postgres';
SET IntervalStyle to postgres_verbose;

SELECT '' AS ten, * FROM INTERVAL_TBL ORDER BY f1;

-- test avg(interval), which is somewhat fragile since people have been
-- known to change the allowed input syntax for type interval without
-- updating pg_aggregate.agginitval

select avg(f1) from interval_tbl;

-- test long interval input
select '4 millenniums 5 centuries 4 decades 1 year 4 months 4 days 17 minutes 31 seconds'::interval;

-- test long interval output
select '100000000y 10mon -1000000000d -1000000000h -10min -10.000001s ago'::interval;

-- test justify_hours() and justify_days()

SELECT justify_hours(interval '6 months 3 days 52 hours 3 minutes 2 seconds') as "6 mons 5 days 4 hours 3 mins 2 seconds";
SELECT justify_days(interval '6 months 36 days 5 hours 4 minutes 3 seconds') as "7 mons 6 days 5 hours 4 mins 3 seconds";

-- test justify_interval()

SELECT justify_interval(interval '1 month -1 hour') as "1 month -1 hour";

SELECT justify_interval('5 mon -50 days'::interval);
SELECT justify_interval('1 mon -47 days'::interval);
SELECT justify_interval('1 mon -48 days'::interval);
SELECT justify_interval('1 mon -48 days'::interval);
select justify_interval('5 mon -2147483648 days'::interval);
select justify_interval('5 mon -2147483648 days -128 hours'::interval);
select justify_interval('5 mon -2147483649 days'::interval);

-- test fractional second input, and detection of duplicate units
SET DATESTYLE = 'ISO';
SET IntervalStyle TO postgres;

SELECT '1 millisecond'::interval, '1 microsecond'::interval,
       '500 seconds 99 milliseconds 51 microseconds'::interval;
SELECT '3 days 5 milliseconds'::interval;

SELECT '1 second 2 seconds'::interval;              -- error
SELECT '10 milliseconds 20 milliseconds'::interval; -- error
SELECT '5.5 seconds 3 milliseconds'::interval;      -- error
SELECT '1:20:05 5 microseconds'::interval;          -- error
SELECT '1 day 1 day'::interval;                     -- error
SELECT interval '1-2';  -- SQL year-month literal
SELECT interval '999' second;  -- oversize leading field is ok
SELECT interval '999' minute;
SELECT interval '999' hour;
SELECT interval '999' day;
SELECT interval '999' month;

-- test SQL-spec syntaxes for restricted field sets
SELECT interval '1' year;
SELECT interval '2' month;
SELECT interval '3' day;
SELECT interval '4' hour;
SELECT interval '5' minute;
SELECT interval '6' second;
SELECT interval '1' year to month;
SELECT interval '1-2' year to month;
SELECT interval '1 2' day to hour;
SELECT interval '1 2:03' day to hour;
SELECT interval '1 2:03:04' day to hour;
SELECT interval '1 2' day to minute;
SELECT interval '1 2:03' day to minute;
SELECT interval '1 2:03:04' day to minute;
SELECT interval '1 2' day to second;
SELECT interval '1 2:03' day to second;
SELECT interval '1 2:03:04' day to second;
SELECT interval '1 2' hour to minute;
SELECT interval '1 2:03' hour to minute;
SELECT interval '1 2:03:04' hour to minute;
SELECT interval '1 2' hour to second;
SELECT interval '1 2:03' hour to second;
SELECT interval '1 2:03:04' hour to second;
SELECT interval '1 2' minute to second;
SELECT interval '1 2:03' minute to second;
SELECT interval '1 2:03:04' minute to second;
SELECT interval '1 +2:03' minute to second;
SELECT interval '1 +2:03:04' minute to second;
SELECT interval '1 -2:03' minute to second;
SELECT interval '1 -2:03:04' minute to second;
SELECT interval '123 11' day to hour; -- ok
SELECT interval '123 11' day; -- not ok
SELECT interval '123 11'; -- not ok, too ambiguous
SELECT interval '123 2:03 -2:04'; -- not ok, redundant hh:mm fields

-- test syntaxes for restricted precision
SELECT interval(0) '1 day 01:23:45.6789';
SELECT interval(2) '1 day 01:23:45.6789';
SELECT interval '12:34.5678' minute to second(2);  -- per SQL spec
SELECT interval(2) '12:34.5678' minute to second;  -- historical PG
SELECT interval(2) '12:34.5678' minute to second(2);  -- syntax error
SELECT interval '1.234' second;
SELECT interval '1.234' second(2);
SELECT interval '1 2.345' day to second(2);
SELECT interval '1 2:03' day to second(2);
SELECT interval '1 2:03.4567' day to second(2);
SELECT interval '1 2:03:04.5678' day to second(2);
SELECT interval '1 2.345' hour to second(2);
SELECT interval '1 2:03.45678' hour to second(2);
SELECT interval '1 2:03:04.5678' hour to second(2);
SELECT interval '1 2.3456' minute to second(2);
SELECT interval '1 2:03.5678' minute to second(2);
SELECT interval '1 2:03:04.5678' minute to second(2);
--check interval concat with null
SELECT interval '1 2:03:04.5678' minute to second(2) || '';

-- test casting to restricted precision (bug #14479)
SELECT f1, 
f1
::INTERVAL MINUTE TO SECOND
::INTERVAL HOUR TO SECOND
::INTERVAL DAY TO SECOND
::INTERVAL HOUR TO MINUTE
::INTERVAL DAY TO MINUTE
AS "minutes",
f1
::INTERVAL DAY TO HOUR
::INTERVAL YEAR TO MONTH
AS "monthes",
(f1 + INTERVAL '1 month')
::INTERVAL SECOND
::INTERVAL MINUTE
::INTERVAL HOUR
::INTERVAL DAY
::INTERVAL MONTH
::INTERVAL YEAR 
AS "years"
FROM interval_tbl order by f1;

-- test inputting and outputting SQL standard interval literals
SET IntervalStyle TO sql_standard;
SELECT  interval '0'                       AS "zero",
        interval '1-2' year to month       AS "year-month",
        interval '1 2:03:04' day to second AS "day-time",
        - interval '1-2'                   AS "negative year-month",
        - interval '1 2:03:04'             AS "negative day-time";

-- test input of some not-quite-standard interval values in the sql style
SET IntervalStyle TO postgres;
SELECT  interval '+1 -1:00:00',
        interval '-1 +1:00:00',
        interval '+1-2 -3 +4:05:06.789',
        interval '-1-2 +3 -4:05:06.789';

-- test output of couple non-standard interval values in the sql style
SET IntervalStyle TO sql_standard;
SELECT  interval '1 day -1 hours',
        interval '-1 days +1 hours',
        interval '1 years 2 months -3 days 4 hours 5 minutes 6.789 seconds',
        - interval '1 years 2 months -3 days 4 hours 5 minutes 6.789 seconds';

-- test outputting iso8601 intervals
SET IntervalStyle to iso_8601;
select  interval '0'                                AS "zero",
        interval '1-2'                              AS "a year 2 months",
        interval '1 2:03:04'                        AS "a bit over a day",
        interval '2:03:04.45679'                    AS "a bit over 2 hours",
        (interval '1-2' + interval '3 4:05:06.7')   AS "all fields",
        (interval '1-2' - interval '3 4:05:06.7')   AS "mixed sign",
        (- interval '1-2' + interval '3 4:05:06.7') AS "negative";

-- test inputting ISO 8601 4.4.2.1 "Format With Time Unit Designators"
SET IntervalStyle to sql_standard;
select  interval 'P0Y'                    AS "zero",
        interval 'P1Y2M'                  AS "a year 2 months",
        interval 'P1W'                    AS "a week",
        interval 'P1DT2H3M4S'             AS "a bit over a day",
        interval 'P1Y2M3DT4H5M6.7S'       AS "all fields",
        interval 'P-1Y-2M-3DT-4H-5M-6.7S' AS "negative",
        interval 'PT-0.1S'                AS "fractional second";

-- test inputting ISO 8601 4.4.2.2 "Alternative Format"
SET IntervalStyle to postgres;
select  interval 'P00021015T103020'       AS "ISO8601 Basic Format",
        interval 'P0002-10-15T10:30:20'   AS "ISO8601 Extended Format";

-- Make sure optional ISO8601 alternative format fields are optional.
select  interval 'P0002'                  AS "year only",
        interval 'P0002-10'               AS "year month",
        interval 'P0002-10-15'            AS "year month day",
        interval 'P0002T1S'               AS "year only plus time",
        interval 'P0002-10T1S'            AS "year month plus time",
        interval 'P0002-10-15T1S'         AS "year month day plus time",
        interval 'PT10'                   AS "hour only",
        interval 'PT10:30'                AS "hour minute";

-- test a couple rounding cases that changed since 8.3 w/ HAVE_INT64_TIMESTAMP.
SET IntervalStyle to postgres_verbose;
select interval '-10 mons -3 days +03:55:06.70';
select interval '1 year 2 mons 3 days 04:05:06.699999';
select interval '0:0:0.7', interval '@ 0.70 secs', interval '0.7 seconds';

-- check that '30 days' equals '1 month' according to the hash function
select '30 days'::interval = '1 month'::interval as t;
select interval_hash('30 days'::interval) = interval_hash('1 month'::interval) as t;

SELECT 'ABC' || CAST(NULL AS INTERVAL); 
SELECT '烦%￥' || CAST(NULL AS INTERVAL); 

-- test about cast
select 15::int1::INTERVAL MINUTE;
select 15::int2::INTERVAL MINUTE;
select 15::int4::INTERVAL MINUTE;
select 15::float8::INTERVAL MINUTE;
select 15::numeric::INTERVAL MINUTE;
select 15::text::INTERVAL MINUTE;
select 15::varchar::INTERVAL MINUTE;
select 15::bpchar(2)::INTERVAL MINUTE;
select '15'::int1::INTERVAL MINUTE;
select '15'::int2::INTERVAL MINUTE;
select '15'::int4::INTERVAL MINUTE;
select '15'::float8::INTERVAL MINUTE;
select '15'::numeric::INTERVAL MINUTE;
select '15'::text::INTERVAL MINUTE;
select '15'::varchar::INTERVAL MINUTE;
select '15'::bpchar(2)::INTERVAL MINUTE;

select 15::int1::INTERVAL HOUR;
select 15::int2::INTERVAL HOUR;
select 15::int4::INTERVAL HOUR;
select 15::float8::INTERVAL HOUR;
select 15::numeric::INTERVAL HOUR;
select 15::text::INTERVAL HOUR;
select 15::varchar::INTERVAL HOUR;
select 15::bpchar(2)::INTERVAL HOUR;
select '15'::int1::INTERVAL HOUR;
select '15'::int2::INTERVAL HOUR;
select '15'::int4::INTERVAL HOUR;
select '15'::float8::INTERVAL HOUR;
select '15'::numeric::INTERVAL HOUR;
select '15'::text::INTERVAL HOUR;
select '15'::varchar::INTERVAL HOUR;
select '15'::bpchar(2)::INTERVAL HOUR;

select 15::int1::INTERVAL ;
select 15::int2::INTERVAL ;
select 15::int4::INTERVAL ;
select 15::float8::INTERVAL ;
select 15::numeric::INTERVAL ;
select 15::text::INTERVAL ;
select 15::varchar::INTERVAL ;
select 15::bpchar(2)::INTERVAL ;
select '15'::int1::INTERVAL ;
select '15'::int2::INTERVAL ;
select '15'::int4::INTERVAL ;
select '15'::float8::INTERVAL ;
select '15'::numeric::INTERVAL ;
select '15'::text::INTERVAL ;
select '15'::varchar::INTERVAL ;
select '15'::bpchar(2)::INTERVAL ;

select 15::int1::INTERVAL YEAR;
select 15::int2::INTERVAL YEAR;
select 15::int4::INTERVAL YEAR;
select 15::float8::INTERVAL YEAR;
select 15::numeric::INTERVAL YEAR;
select 15::text::INTERVAL YEAR;
select 15::varchar::INTERVAL YEAR;
select 15::bpchar(2)::INTERVAL YEAR;
select '15'::int1::INTERVAL YEAR;
select '15'::int2::INTERVAL YEAR;
select '15'::int4::INTERVAL YEAR;
select '15'::float8::INTERVAL YEAR;
select '15'::numeric::INTERVAL YEAR;
select '15'::text::INTERVAL YEAR;
select '15'::varchar::INTERVAL YEAR;
select '15'::bpchar(2)::INTERVAL YEAR;

-- test abourt interval typmod in procedure
drop table if exists all_datatype_tbl;
create table all_datatype_tbl(
        c_id integer,
        c_boolean boolean,
        c_integer integer, c_bigint bigint,
        c_real real,
        c_decimal decimal(38), c_number number(38),
        c_char char(50) default null, c_varchar varchar(50), c_clob clob,
    c_blob blob,
         c_timestamp timestamp, c_interval interval day to second) with (segment=on) ;
create or replace procedure pro_012()
as
    sqlstat varchar(500);
        v1 interval day to second;
begin
    v1 := '12 12:3:4.1234';
    sqlstat := 'insert into all_datatype_tbl(c_interval) select :p1';
    execute immediate sqlstat using v1;
end;
/
call pro_012();
select c_interval from all_datatype_tbl;
drop procedure pro_012;
create or replace procedure pro_015()
as
    sqlstat varchar(500);
        v1 interval day to second;
        r1 interval day to second;
begin
    v1 := '12 12:3:4.1234';
    sqlstat := 'select :p1';
    execute immediate sqlstat into r1 using v1;
    raise info 'result:%',v1;
end;
/
call pro_015();
drop procedure pro_015;
