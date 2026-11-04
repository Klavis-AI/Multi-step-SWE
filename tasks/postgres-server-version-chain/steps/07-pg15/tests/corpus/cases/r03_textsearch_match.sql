SELECT to_tsvector('english', 'the quick brown fox') @@ websearch_to_tsquery('english', 'quick fox');
SELECT to_tsvector('english', 'the quick brown fox') @@ websearch_to_tsquery('english', 'quick -fox');
SELECT to_tsvector('english', 'the quick brown fox') @@ websearch_to_tsquery('english', '"brown fox"');
SELECT websearch_to_tsquery('english', '');
SELECT websearch_to_tsquery('english', 'or or or');
