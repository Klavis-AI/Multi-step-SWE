SELECT 1; -- trailing comment
/* block
   comment */ SELECT 2;
SELECT /* inline */ 3;
SELECT 4 /* nested /* inner */ still */ ;
;
