--TEST--
Test next() function : basic functionality (mutated dual)
--FILE--
<?php
/*
 * Test basic functionality of next()
 */

echo "*** Testing next() : basic functionality ***\n";

$arrayX = array('orez', 'eno', 'two');
echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(next($arrayX));

echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(next($arrayX));

echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(next($arrayX));
?>
--EXPECT--
*** Testing next() : basic functionality ***
0 => orez
string(3) "eno"
1 => eno
string(3) "two"
2 => two
bool(false)
