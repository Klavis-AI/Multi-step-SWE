-- R02.7: json_pretty must indent exactly as SQLite's does: four spaces per
-- level,
-- one element per line, nested arrays indented again. The whole point of the
-- function is its layout, so the layout is what is graded.
--
-- The last two statements exercise json_pretty's optional second argument, the
-- indent string, which replaces the four-space default. R02.7 grades "the
-- layout its optional second argument selects instead", so the layout is
-- compared byte for byte, exactly as the four-space default is above; a length
-- alone would accept a 12-byte rendering that indented or spaced differently.
-- Measured against stock sqlite3 3.50.2 and against every checkpoint from this
-- one onward: all fifteen render '{"a":1}' at a two-space indent as the same
-- three lines, '{', '  "a": 1', '}'.
SELECT json_pretty('{"a":1,"b":[1,2],"c":{"d":"x"}}');
SELECT json_pretty('[1,[2,[3]]]');
SELECT json_pretty('{"s":"text","n":null,"t":true,"f":false,"r":1.5}');
SELECT length(json_pretty('{"a":1}'));
SELECT json_pretty('7');
SELECT json_pretty('{"a":1}','  ');
SELECT length(json_pretty('{"a":1}','  '));
