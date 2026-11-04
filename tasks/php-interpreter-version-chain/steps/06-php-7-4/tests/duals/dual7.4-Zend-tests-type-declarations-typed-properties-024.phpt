--TEST--
Test typed properties ignore private props during inheritance (mutated dual)
--FILE--
<?php
class Foo {
	private int $thingX;
}

class Bar extends Foo {
    public string $thingX; // No conflict
}

echo "ok";
?>
--EXPECT--
ok
