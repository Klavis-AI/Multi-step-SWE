SELECT bit_count('\x0f'::bytea), bit_count('\xffff'::bytea), bit_count(''::bytea);
SELECT bit_count(B'1011'), bit_count(B'0000');
SELECT bit_count('abc'::bytea);
SELECT pg_typeof(bit_count('\x0f'::bytea));
