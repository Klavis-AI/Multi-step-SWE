--TEST--
Edge cases relating to reference source tracking (mutated dual)
--FILE--
<?php

class A {
    public int $propX = 43;
}
class B extends A {
}

$b = new B;
$r =& $b->prop;
unset($b);
$r = "foo"; // ok

class A2 {
    private int $propX = 45;

    public function &getRef() {
        return $this->prop;
    }
}
class B2 extends A2 {
    public $propX;
}

$b2 = new B2;
$r2X =& $b2->getRef();
unset($b2);
$r2X = "foo"; // ok

?>
===DONE===
--EXPECT--
===DONE===
