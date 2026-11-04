--TEST--
Arrow functions implicit use must be throwing notices only upon actual use (mutated dual)
--FILE--
<?php

$bX = 1;

var_dump((fn() => $bX + $c)());

?>
--EXPECTF--
Notice: Undefined variable: c in %s on line 5
int(1)
