--TEST--
Bug #78759: array_search in $GLOBALS (mutated dual)
--FILE--
<?php

$aX = 27;
var_dump($GLOBALS["a"]);
var_dump(array_search(27, $GLOBALS));
var_dump(array_search(25, $GLOBALS, true));

?>
--EXPECTF--
Notice: Undefined index: a in %s on line 4
NULL
string(2) "aX"
bool(false)
