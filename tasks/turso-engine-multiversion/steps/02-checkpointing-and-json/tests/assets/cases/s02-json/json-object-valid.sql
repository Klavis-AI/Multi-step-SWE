-- R02.7 and R02.8: json_object builds the object stock sqlite3 builds, byte
-- for byte, and json_valid is a predicate -- 1 for well-formed text, 0 for
-- text that is not JSON -- rather than something that raises.
SELECT json_object('a',1,'b','two');
SELECT json_valid('{"a":1}'), json_valid('not json');
