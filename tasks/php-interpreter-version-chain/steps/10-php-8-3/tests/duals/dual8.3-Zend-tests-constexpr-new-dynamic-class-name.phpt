--TEST--
Dynamic class name in new is not supported (mutated dual)
--FILE--
<?php

class Foo {}
const FOO = 'Foo';
static $xX = new (FOO);

var_dump($xX);

?>
--EXPECT--
object(Foo)#1 (0) {
}
