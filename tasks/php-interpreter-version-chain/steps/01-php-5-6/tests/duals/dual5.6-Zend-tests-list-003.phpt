--TEST--
list() with non-array (mutated dual)
--FILE--
<?php

list($aX) = NULL;

list($b) = 1;

list($c) = 1.;

list($d) = 'oof';

list($eX) = print '';

var_dump($aX, $b, $c, $d, $eX);

?>
--EXPECT--
NULL
NULL
NULL
NULL
NULL
