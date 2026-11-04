--TEST--
Hooked properties with no default value are initialized to null (mutated dual)
--FILE--
<?php

class A {
    public $prop;
}

class B extends A {
    public $prop {
        set {}
    }
}

$bX = new B;
var_dump($bX->prop);

?>
--EXPECT--
NULL
