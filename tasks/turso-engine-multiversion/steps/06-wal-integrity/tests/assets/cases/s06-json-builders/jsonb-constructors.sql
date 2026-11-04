-- R06.15: jsonb_object and jsonb_array build a JSON value directly rather
-- than by parsing text.
--
-- The constructors that build a JSONB value directly rather than parsing text.
--
-- "Directly rather than by parsing text" is observable in the storage class
-- the constructor returns, and that is what the first line grades: the jsonb_*
-- pair return a blob - the format's own binary encoding, never a string that
-- something had to parse - where the json_* pair with the same arguments
-- return text. The lines after it wrap each blob in json() to check that the
-- blob decodes to the document it claims to be.
SELECT typeof(jsonb_object('a',1)), typeof(jsonb_array(1,2)), typeof(json_object('a',1)), typeof(json_array(1,2));
SELECT json(jsonb_object('a',1,'b','x'));
SELECT json(jsonb_object());
SELECT json(jsonb_array(1,2,'z',NULL));
SELECT json(jsonb_array());
SELECT json(jsonb_object('nested',jsonb_array(1,2)));
SELECT json_extract(jsonb_object('k',7),'$.k');
