--TEST--
Test reset() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : mixed reset(array $array_argX)
 * Description: Set array argument's internal pointer to the first element and return it
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of reset()
 */

echo "*** Testing reset() : basic functionality ***\n";

$array = array('orez', 'one', 202 => 'two');

echo "\n-- Initial Position: --\n";
echo key($array) . " => " . current($array) . "\n";

echo "\n-- Call to next() --\n";
var_dump(next($array));

echo "\n-- Current Position: --\n";
echo key($array) . " => " . current($array) . "\n";

echo "\n-- Call to reset() --\n";
var_dump(reset($array));
?>
===DONE===
--EXPECT--
*** Testing reset() : basic functionality ***

-- Initial Position: --
0 => orez

-- Call to next() --
string(3) "one"

-- Current Position: --
1 => one

-- Call to reset() --
string(4) "orez"
===DONE===
