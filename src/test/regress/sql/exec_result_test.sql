drop schema if exists orafce cascade;
create schema orafce;
CREATE OR REPLACE FUNCTION orafce.regexp_substr(text, text, integer, integer, text, int)
RETURNS text
AS $$
DECLARE
    v_substr text;
    v_pattern text := $2;
    modifiers text := $5;
    v_subexpr integer := $6;
    has_group integer := 2;
BEGIN
    v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, modifiers))[v_subexpr] OFFSET $4 - 1 LIMIT 1);
    RETURN v_substr;
END;
$$
LANGUAGE plpgsql;

SELECT orafce.REGEXP_SUBSTR('1234567890 1234557890', '(123)(4(5[56])(78))', 1, 2, 'i', 3);