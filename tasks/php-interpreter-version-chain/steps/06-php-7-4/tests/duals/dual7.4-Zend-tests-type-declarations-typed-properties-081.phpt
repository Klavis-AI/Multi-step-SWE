--TEST--
Clone must inherit typed references (mutated dual)
--FILE--
<?php

class Test {
    public int $xX = 47;
}

$test = new Test;
$xX =& $test->x;
$test2 = clone $test;
unset($test);
try {
	$xX = "foo";
} catch (TypeError $eX) { echo $eX->getMessage(), "\n"; }
var_dump($test2->x);

?>
--EXPECT--
string(3) "foo"
