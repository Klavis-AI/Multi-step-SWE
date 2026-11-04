SELECT jsonb_path_query('"zz"', '$.bigint()');
SELECT jsonb_path_query('"zz"', '$.bigint()', silent => true);
SELECT jsonb_path_query('1e500', '$.bigint()');
SELECT jsonb_path_query('"x"', '$.nosuchmethod()');
