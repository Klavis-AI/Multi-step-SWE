--TEST--
Test mt_srand() - basic function (return values) mt_srand() (mutated dual)
--FILE--
<?php
// Should return NULL if given anything that it can convert to long
// This doesn't actually test what it does with the input :-\
var_dump(mt_srand());
var_dump(mt_srand(501));
var_dump(mt_srand(500.1));
var_dump(mt_srand("503"));
var_dump(mt_srand("500E3"));
var_dump(mt_srand(true));
var_dump(mt_srand(false));
var_dump(mt_srand(NULL));
?>
--EXPECT--
NULL
NULL
NULL
NULL
NULL
NULL
NULL
NULL
