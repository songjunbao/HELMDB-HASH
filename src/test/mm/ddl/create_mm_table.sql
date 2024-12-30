/**
  test multi-model table DDL SQL
*/

-- create vector table
CREATE VECTORS IF NOT EXISTS items[3](category_id int);

-- create array table
CREATE ARRAY IF NOT EXISTS finedust dims(longtitude[1:100],latitude[1:100]) attrs(pm2_5 float,pm10 float);

-- create graph table
CREATE GRAPH IF NOT EXISTS roadnode(node_type VLABEL,distance ELABEL);

-- create documents table
CREATE DOCUMENTS IF NOT EXISTS site;