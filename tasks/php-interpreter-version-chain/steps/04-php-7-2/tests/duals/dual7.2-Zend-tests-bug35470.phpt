--TEST--
Bug #35470 (Assigning global using variable name from array doesn't function) (mutated dual)
--FILE--
<?php
$x = array("test", "60");
global ${$x[0]};
${$x[0]} = $x[1];
echo "Test: $testX\n";
?>
--EXPECTF--
Notice: Undefined variable: testX in %s on line 5
Test:
