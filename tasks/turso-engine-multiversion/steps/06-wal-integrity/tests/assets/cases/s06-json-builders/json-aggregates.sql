-- R06.15: json_group_array and json_group_object collect a result set into a
-- single document.
--
-- The JSON aggregate functions collect a result set into one document.
--
-- Every collecting call below draws from a subquery that carries an ORDER BY,
-- because the order in which an aggregate visits its input is not part of the
-- contract: without one, the document's element order would be whatever the
-- scan happened to produce, and the expected output would be asserting the
-- query planner rather than the aggregate. The three assertions that follow the
-- collecting ones do not depend on order at all - a length, the two document
-- types, and three lookups by key - so the collection is graded twice over,
-- once with an order imposed and once in a form no order can change.
CREATE TABLE t(k TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('b',2),('c',3);
SELECT json_group_array(v) FROM (SELECT v FROM t ORDER BY k);
SELECT json_group_object(k,v) FROM (SELECT k,v FROM t ORDER BY k);
SELECT json_group_array(k) FROM (SELECT k FROM t WHERE v>1 ORDER BY k);
SELECT json_array_length(json_group_array(v)) FROM t;
SELECT json_array_length(json_group_array(k)) FROM t WHERE v>1;
SELECT json_type(json_group_array(v)), json_type(json_group_object(k,v)) FROM t;
SELECT json_extract(json_group_object(k,v),'$.a'), json_extract(json_group_object(k,v),'$.b'), json_extract(json_group_object(k,v),'$.c') FROM t;
