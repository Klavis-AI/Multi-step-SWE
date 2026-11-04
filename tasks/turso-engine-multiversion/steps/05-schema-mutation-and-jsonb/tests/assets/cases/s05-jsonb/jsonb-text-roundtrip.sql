-- R05.7: json(jsonb(X)) returns X's canonical JSON text, so the binary form is
-- a faithful representation of the value and not a lossy cache of it.
--
-- One SELECT per value kind: objects, arrays, integers, reals, both booleans,
-- null, strings including an escaped one, arbitrary nesting, and a jsonb() of
-- a jsonb() to show the conversion is idempotent.
--
-- The last statement takes hex() rather than json(), because the round trip
-- above cannot tell the two string encodings apart: an escaped string comes
-- back as the same text whether it was stored as TEXT (type 7) with the escape
-- already resolved, or as TEXTJ (type 8) with the escape kept as the two
-- characters it was written as. TEST_INTERFACE.md's element-header table fixes
-- which of the two SQLite writes, and this is where that is asserted -- `ab`
-- is 276162, two payload bytes of TEXT, and `a\nb` is 48615C6E62, four payload
-- bytes of TEXTJ carrying the backslash and the n separately. Both are the
-- bytes stock sqlite3 3.50.2 writes for the same two strings.
SELECT json(jsonb('{"a":1,"b":[1,2,3],"c":"x"}'));
SELECT json(jsonb('[1,2.5,true,false,null,"s"]'));
SELECT json(jsonb('{"nested":{"deep":[{"k":"v"}]}}'));
SELECT json(jsonb('[]')), json(jsonb('{}')), json(jsonb('null'));
SELECT json(jsonb('"quoted \"inner\" text"'));
SELECT json(jsonb(jsonb('{"n":[1,{"m":null}]}')));
SELECT hex(jsonb('"ab"')), hex(jsonb('"a\nb"'));
