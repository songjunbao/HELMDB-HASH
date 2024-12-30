-- DDL测试
CREATE TYPE NameType AS (first_name VARCHAR(50),last_name VARCHAR(50));

CREATE DOCUMENTS Persons(
	name NameType,
	friends_id int[]
);

-- DML测试:insert功能和文档生成表达式功能(JSONARRAY[]运算符和{}运算符)
insert into DOCUMENTS Persons(id,name,friends_id,doc) values (1,ROW('aa','bb'),ARRAY[2,3,4],{detail:{email:'e1@xx.com',phone_number:'p1'},birth:'2000-1-1'}::jsonb),
(2,ROW('cc','dd'),ARRAY[1,3,4],{detail:{email:'e2@xx.com',phone_number:'p2'},birth:'2001-3-1'}::jsonb);


-- DQL测试:目前只支持简单的文档path表达式 例如'.'和'[]'组成的表达式

-- 以下三者等价
SELECT * from Persons;
SELECT Persons.name.first_name from Persons; 
SELECT name.first_name from Persons; 

-- 以下三者等价
SELECT detail.email from Persons;
SELECT doc.detail.email from Persons;
SELECT Persons.doc.detail.email from Persons;

-- DML测试:update功能,目前只支持粗粒度的更新，不能单独更新doc字段内的文档结构
update DOCUMENTS Persons set Persons.name = ROW('ee','ff') where Persons.detail.email = '"e2@xx.com"'::jsonb;

-- DML测试:delete功能
delete from DOCUMENTS Persons where Persons.detail.email = '"e1@xx.com"'::jsonb;

-- DQL测试:文档生成表达式功能
select {a:{b:'1'},c: 2,d: true,e:[1,2,3],"k":[{k1: 1},{k2: 2},NULL]};

-- DQL测试:由json_array_elements生成的文档也可以使用文档path表达式
select k.a from json_array_elements(JSONARRAY[{a: 1},{a: 2},{a: 3}]) k;

-- DQL测试:文档数组的UNNSET功能(目前的unnest只支持文档数组(即jsonb类型)，不支持普通数组(ARRAY类型))
create documents mydoc;
insert into documents mydoc values (1,{vv:[[{k: 1},{k: 2}],[{k: 3},{k: 4}]]}::jsonb),(2,{vv:[[{k: 5},{k: 6}],[{k: 7},{k: 8}]]}::jsonb);
select mydoc.id,v1,v2.k from mydoc UNWIND json_array_elements((mydoc.vv)::json) as v1 UNWIND json_array_elements((v1)::json) as v2;
