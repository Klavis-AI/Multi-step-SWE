-- R05.11: the element header byte jsonb() writes for each JSON value kind --
-- the element type in its low nibble, the payload size in its high nibble.
--
-- hex(jsonb(X)) is compared for null, the two booleans, integers of three
-- widths, two real spellings, a short string, an empty array, an empty object,
-- and arrays and objects with elements inside them, including nesting. These
-- are the bytes stock sqlite3 3.50.2 writes for the same values; a different
-- header layout is a different file format.
--
-- The nibble layout these bytes follow -- every element type code, and the
-- rule that picks between an inline size and a 1-, 2- or 4-byte size field --
-- is stated in milestone 5's edition of TEST_INTERFACE.md, the step that
-- introduced jsonb(); that edition is the one this case was authored against
-- and later editions of the document replace it. The case is carried forward
-- as the inherited re-check of that writer, so nothing here has to be
-- reverse-engineered from the expected output.
SELECT hex(jsonb('null')), hex(jsonb('true')), hex(jsonb('false'));
SELECT hex(jsonb('0')), hex(jsonb('1')), hex(jsonb('-1')), hex(jsonb('123456'));
SELECT hex(jsonb('1.5')), hex(jsonb('1e3'));
SELECT hex(jsonb('"a"')), hex(jsonb('[]')), hex(jsonb('{}'));
SELECT hex(jsonb('[1,2]')), hex(jsonb('{"k":"v"}'));
SELECT hex(jsonb('[true,false,null]')), hex(jsonb('[[1]]'));
