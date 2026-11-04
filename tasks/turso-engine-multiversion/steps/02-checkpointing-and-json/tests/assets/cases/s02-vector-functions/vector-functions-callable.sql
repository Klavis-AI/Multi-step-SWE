-- R02.11: the vector functions that arrive with this checkpoint are callable
-- and
-- return a value rather than an unknown-function diagnostic. A vector is an
-- opaque blob as far as the storage classes are concerned, it prints back as
-- the text it was built from, and each distance function answers the three
-- cases whose value is fixed by the definition: identical vectors, orthogonal
-- vectors, and opposed vectors.
SELECT 'type', typeof(vector('[1,2,3]'));
SELECT 'extract', vector_extract(vector('[1,2]'));
SELECT 'extract three', vector_extract(vector('[1,2,3]'));
SELECT 'cos same', vector_distance_cos(vector('[1,0]'), vector('[1,0]'));
SELECT 'cos orthogonal', vector_distance_cos(vector('[1,0]'), vector('[0,1]'));
SELECT 'cos opposed', vector_distance_cos(vector('[1,0]'), vector('[-1,0]'));
SELECT 'is blob', typeof(vector('[1,2]')) = 'blob', length(vector('[1,2]')) > 0;
