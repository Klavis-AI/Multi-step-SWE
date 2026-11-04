--TEST--
Test next() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : mixed next(array $array_argX)
 * Description: Move array argument's internal pointer to the next element and return it
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of next()
 */

echo "*** Testing next() : basic functionality ***\n";

$array = array('orez', 'eno', 'two');
echo key($array) . " => " . current($array) . "\n";
var_dump(next($array));

echo key($array) . " => " . current($array) . "\n";
var_dump(next($array));

echo key($array) . " => " . current($array) . "\n";
var_dump(next($array));
?>
===DONE===
--EXPECT--
*** Testing next() : basic functionality ***
0 => orez
string(3) "eno"
1 => eno
string(3) "two"
2 => two
bool(false)
===DONE===
