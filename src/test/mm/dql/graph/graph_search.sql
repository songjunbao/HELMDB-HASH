-- 选择和投影：查询选择特定年份的所有文献，并投影出文献id和第一作者
/**
等价于Agensgraph中：
  SET graph_path = citation_network;
  SELECT p. id AS Publication_ID, p.first_author AS First_Author
  FROM (
    MATCH (p:Publication)
    WHERE p.year= 202
  )
*/
SELECT *
FROM citation_network MATCH {(p1: publication)-[rel: cites]->(p2: publication)}
where "p1.properties"->>'year' = 2020;

explain SELECT *
FROM citation_network MATCH {(p1: publication)-[rel: cites]->(p2: publication)}
where "p1.properties"->>'year' = 2020;