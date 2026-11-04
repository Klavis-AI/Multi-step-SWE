SELECT websearch_to_tsquery('english', 'cat dog');
SELECT websearch_to_tsquery('english', 'cat or dog');
SELECT websearch_to_tsquery('english', 'cat -dog');
SELECT websearch_to_tsquery('english', '"quick brown"');
SELECT websearch_to_tsquery('simple', 'a or b -c');
