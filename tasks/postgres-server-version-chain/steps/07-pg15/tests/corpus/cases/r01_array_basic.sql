SELECT ARRAY[1,2,3], ARRAY[[1,2],[3,4]], '{1,2,3}'::int[];
SELECT array_length(ARRAY[1,2,3],1), array_dims(ARRAY[[1,2],[3,4]]), array_upper(ARRAY[1,2],1), cardinality(ARRAY[1,2,3]);
SELECT ARRAY[1,2,3][2], (ARRAY[1,2,3])[1:2], ARRAY[1,2,3][9];
SELECT ARRAY[1,2] || 3, 0 || ARRAY[1,2], ARRAY[1,2] || ARRAY[3,4];
SELECT array_append(ARRAY[1,2],3), array_prepend(0,ARRAY[1,2]), array_remove(ARRAY[1,2,2],2);
SELECT array_position(ARRAY['a','b','c'],'b'), array_positions(ARRAY[1,2,1],1);
SELECT array_to_string(ARRAY[1,2,3],'-'), string_to_array('a,b,c',',');
