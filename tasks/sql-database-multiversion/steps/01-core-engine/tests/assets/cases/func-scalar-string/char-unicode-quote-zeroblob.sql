SELECT char(65, 66, 67), unicode('A'), unicode('ABC');
SELECT quote('it''s'), quote(42);
SELECT typeof(zeroblob(4)), length(zeroblob(4)), hex(zeroblob(3));
