drop schema if exists plpgsql_pkg_dependency cascade;
create schema plpgsql_pkg_dependency;
set current_schema = plpgsql_pkg_dependency;
set behavior_compat_options = 'plpgsql_dependency';

-- test 1
create or replace package test_package_depend2_pkg is
type t is record (col1 int, col2 text);
procedure p1(param test_package_depend3_pkg.t);
end test_package_depend2_pkg;
/
create  or replace package body test_package_depend2_pkg is
procedure p1(param test_package_depend3_pkg.t) is
    begin
        RAISE INFO 'call param: %', param;
    end;
end test_package_depend2_pkg;
/

create or replace package test_package_depend3_pkg is
type t is record (col1 int, col2 text, col3 varchar);
procedure p1(param test_package_depend2_pkg.t);
end test_package_depend3_pkg;
/
create  or replace  package body test_package_depend3_pkg is
procedure p1(param test_package_depend2_pkg.t) is
    begin
        RAISE INFO 'call param: %', param;
    end;
end test_package_depend3_pkg;
/

call test_package_depend2_pkg.p1((1,'a','2023'));
call test_package_depend3_pkg.p1((1,'a'));

drop package if exists test_package_depend2_pkg;
drop package if exists test_package_depend3_pkg;

-- test 2
create or replace package pkg1 is
type x is record(tt int);
procedure a(zz pkg2.x);
end pkg1;
/
create or replace package body pkg1 is
procedure a(zz pkg2.x) is
begin
null;
end;
end pkg1;
/

create or replace package pkg2 is
type x is record(tt int);
procedure a(zz pkg1.x);
end pkg2;
/
create or replace package body pkg2 is
procedure a( zz pkg1.x) is
begin
null;
end;
end pkg2;
/

\parallel on 2
begin
perform pkg1.a(row(1));
perform pg_sleep(1.5);
perform pkg2.a(row(1));
perform pg_sleep(2.5);
end;
/

begin
perform pg_sleep(0.5);
perform pkg2.a(row(1));
perform pg_sleep(2.5);
perform pkg1.a(row(1));
perform pg_sleep(2.5);
end;
/
\parallel off

drop package if exists pkg1;
drop package if exists pkg2;

-- clean
drop schema plpgsql_pkg_dependency cascade;
reset behavior_compat_options;