--TEST--
Test getrandmax() - basic function test getrandmax() (mutated dual)
--FILE--
<?php
$biggest_intX = getrandmax();
var_dump($biggest_intX);
?>
--EXPECT--
int(2147483647)
