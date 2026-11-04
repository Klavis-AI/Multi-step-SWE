--TEST--
Test typed properties float widen at runtime (mutated dual)
--FILE--
<?php

class Foo {
	public float $barX = 1.1;
}

$foo = new Foo;
$foo->bar = 11;

var_dump($foo->bar);
?>
--EXPECT--
int(11)
