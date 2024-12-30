-- create graph table
CREATE GRAPH IF NOT EXISTS roadnode(node_type VLABEL,distance ELABEL);


CREATE GRAPH IF NOT EXISTS citation_network(publication VLABEL,cites ELABEL);
