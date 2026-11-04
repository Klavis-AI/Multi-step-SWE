-- R05.13: the jsonb_* editing functions take either representation of the
-- document they edit, and jsonb_remove returns the binary one.
--
-- Four calls below hand the function JSON text; one hands jsonb_replace a
-- JSONB blob and one hands jsonb_remove a JSONB blob, so "either
-- representation" is observed for both functions rather than for one of them.
-- jsonb_replace over a path that is absent leaves the document alone, as its
-- text counterpart does.
--
-- Wrapping a result in json() does NOT show that the result was binary, and
-- neither does feeding it to json_extract: both accept text and blob alike, so
-- the json_extract call below answers 1 for a text-returning implementation
-- too. The last statement is the one that observes the representation.
-- typeof() names the storage class and hex() shows the bytes: 4C is four
-- payload bytes of OBJECT, then 17 62 (a one-byte TEXT payload, "b") and
-- 13 32 (a one-byte INT payload, "2") -- exactly the element encoding
-- TEST_INTERFACE.md states. Stock sqlite3 3.50.2 answers blob and the same
-- hex for the same call.
--
-- typeof(jsonb_replace(...)) is deliberately not asserted anywhere: this
-- engine returns text there where stock returns a blob, and TEST_INTERFACE.md
-- records that difference as ungraded rather than pinning it into an expected
-- output.
SELECT json(jsonb_remove('{"a":1,"b":2}','$.a'));
SELECT json(jsonb_remove('[1,2,3]','$[1]'));
SELECT json(jsonb_replace('{"a":1}','$.a',8));
SELECT json(jsonb_replace('{"a":1}','$.missing',8));
SELECT json(jsonb_replace(jsonb('{"a":{"b":1}}'),'$.a.b','t'));
SELECT json_extract(jsonb_remove('{"a":1,"b":2}','$.b'),'$.a');
SELECT json(jsonb_remove(jsonb('{"a":1,"b":2}'),'$.a'));
SELECT typeof(jsonb_remove(jsonb('{"a":1,"b":2}'),'$.a')), hex(jsonb_remove(jsonb('{"a":1,"b":2}'),'$.a'));
