SELECT '100'::xid8, '0'::xid8, '18446744073709551615'::xid8;
SELECT pg_typeof('100'::xid8);
SELECT '100'::xid8 = '100'::xid8, '3'::xid8 < '4'::xid8, '3'::xid8 <> '4'::xid8;
SELECT '100'::xid8::text;
