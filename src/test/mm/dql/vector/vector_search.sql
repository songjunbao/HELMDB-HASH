/*
 * Some query examples offered by pgvector official website
 */

-- Get the nearest neighbors to a vector
SELECT * FROM items ORDER BY vec <-> '[3,1,2]' LIMIT 5;

-- Get the nearest neighbors to a row
SELECT * FROM items WHERE id != 1 ORDER BY vec <-> (SELECT vec FROM items WHERE id = 1) LIMIT 5;

-- Get rows within a certain distance
SELECT * FROM items WHERE vec <-> '[3,1,2]' < 5;

-- Get the distance
SELECT vec <-> '[3,1,2]' AS distance FROM items;

-- For inner product, multiply by -1 (since <#> returns the negative inner product)
SELECT (vec <#> '[3,1,2]') * -1 AS inner_product FROM items;

-- For cosine similarity, use 1 - cosine distance
SELECT 1 - (vec <=> '[3,1,2]') AS cosine_similarity FROM items;

-- Average vectors
SELECT pg_catalog.AVG(vec) FROM items;

-- Average groups of vectors
SELECT id, pg_catalog.AVG(vec) FROM items GROUP BY id;