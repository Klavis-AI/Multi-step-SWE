SELECT array_dims(array_shuffle(ARRAY[[1,2],[3,4],[5,6]]));
SELECT array_dims(array_sample(ARRAY[[1,2],[3,4],[5,6]],2));
SELECT cardinality(array_sample(ARRAY[[1,2],[3,4],[5,6]],2));
SELECT array_length(array_sample(ARRAY[[1,2],[3,4],[5,6]],2),2);
