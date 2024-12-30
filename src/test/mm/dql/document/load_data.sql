COPY site(doc)
FROM '/home/gauss/dev/mmdbs-openGauss/src/test/mm/dql/document/site.csv'
DELIMITER ','
CSV HEADER;