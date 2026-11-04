-- lang_corefunc.html, length(X): "For a string value X, the length(X) function
-- returns the number of characters (not bytes) in X prior to the first NUL
-- character. ... For a blob value X, length(X) returns the number of bytes in
-- the blob."  PROFILE.md section 4 puts length in scope with that page and
-- section 2 puts CAST and the five storage classes in scope.
SELECT length(CAST(x'610062' AS TEXT));
SELECT length(CAST(x'610062' AS BLOB));
SELECT typeof(CAST(x'610062' AS TEXT));
SELECT length(CAST(x'00616263' AS TEXT));
SELECT length(CAST(x'616263' AS TEXT));
SELECT length(CAST(x'616263' AS BLOB));
