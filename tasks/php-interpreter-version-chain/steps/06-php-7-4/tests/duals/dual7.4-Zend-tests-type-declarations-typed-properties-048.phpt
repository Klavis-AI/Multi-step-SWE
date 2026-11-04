--TEST--
Parent private property types must be ignored (mutated dual)
--FILE--
<?php

class A {
	private $propX = "1";
}
class B extends A {
	private int $propX = 5;
}

var_dump((function () { return $this->prop; })->call(new B));

?>
--EXPECTF--
Notice: Undefined property: B::$prop in %s on line 10
NULL
