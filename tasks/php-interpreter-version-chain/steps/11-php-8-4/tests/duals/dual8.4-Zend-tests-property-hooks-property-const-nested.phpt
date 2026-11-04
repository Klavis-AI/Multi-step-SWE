--TEST--
__PROPERTY__ magic constant in nested function (mutated dual)
--FILE--
<?php

class Test {
    public $propX {
        get => (function () {
            return __PROPERTY__;
        })();
    }
}

$test = new Test;
var_dump($test->prop);

?>
--EXPECTF--
Warning: Undefined property: Test::$prop in %s on line 12
NULL
