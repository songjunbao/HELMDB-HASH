-- create array table
CREATE ARRAY IF NOT EXISTS finedust dims(longtitude[1:100],latitude[1:100]) attrs(pm2_5 float,pm10 float);

-- insert sample values
DO $$
DECLARE
    i INTEGER;
    j INTEGER;
    pm2_5_value DOUBLE PRECISION;
    pm10_value DOUBLE PRECISION;
BEGIN
    FOR i IN 2..31 LOOP
        FOR j IN 2..31 LOOP
            pm2_5_value := RANDOM() * 100;  -- 假设浮点数在0到100之间
            pm10_value := RANDOM() * 100;   -- 假设浮点数在0到100之间
            INSERT INTO finedust (longtitude, latitude, pm2_5, pm10) 
            VALUES (i, j, pm2_5_value, pm10_value);
        END LOOP;
    END LOOP;
END $$;

-- range query 
SELECT_ARRAY finedust[3:5][4:15](pm2_5);

-- point query
SELECT_ARRAY finedust[10][10](pm10);


-- explain query
EXPLAIN SELECT_ARRAY finedust[3:5][4:15](pm2_5);