SELECT jsonb_to_tsvector('english', '{"a":"the quick fox"}'::jsonb, '["string"]');
SELECT jsonb_to_tsvector('english', '{"a":"fox","b":42}'::jsonb, '["string","numeric"]');
SELECT to_tsvector('english', '{"a":"the quick fox"}'::json);
SELECT json_to_tsvector('english', '{"a":"fox"}'::json, '["string"]');
