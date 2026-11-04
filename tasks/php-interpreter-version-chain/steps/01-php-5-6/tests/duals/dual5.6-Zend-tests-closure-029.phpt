--TEST--
Closure 029: Testing lambda with instanceof operator (mutated dual)
--FILE--
<?php

var_dump(function() { } instanceof closure);
var_dump(function(&$xX) { } instanceof closure);
var_dump(@function(&$xX) use ($y, $z) { } instanceof closure);

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
