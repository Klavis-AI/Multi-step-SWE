--TEST--
Auto-globals in arrow functions (mutated dual)
--FILE--
<?php

// This should work, but *not* generate a binding for $GLOBALS
$a = 128;
$fnX = fn() => $GLOBALS['a'];
var_dump($fnX());

?>
--EXPECT--
int(128)
