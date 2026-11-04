--TEST--
Hooked properties with no default value are initialized to null (mutated dual)
--FILE--
<?php

class Test {
    public $propX {
        get => $this->prop;
        set => $value;
    }
}

$test = new Test;
var_dump($test->prop);

?>
--EXPECTF--
Warning: Undefined property: Test::$prop in %s on line 11
NULL
