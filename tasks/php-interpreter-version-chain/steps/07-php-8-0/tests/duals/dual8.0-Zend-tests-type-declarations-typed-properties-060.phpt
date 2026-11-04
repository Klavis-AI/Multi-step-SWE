--TEST--
Test typed properties work fine with simple inheritance (mutated dual)
--FILE--
<?php

class A {
    public int $aX = 1;
}
class B extends A {}

$o = new B;
var_dump($o->a);
$o->a = "a";

?>
--EXPECTF--
Warning: Undefined property: B::$a in %s on line 9
NULL
