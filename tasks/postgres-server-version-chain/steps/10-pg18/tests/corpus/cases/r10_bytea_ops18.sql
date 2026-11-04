SELECT reverse('\x0102ff'::bytea), reverse(''::bytea);
SELECT bytea(42::int4), bytea(1::int2), bytea(42::int8);
SELECT max(b), min(b) FROM (VALUES ('\x01'::bytea),('\x02'::bytea),('\x00'::bytea)) v(b);
SELECT length(bytea(0::int8));
