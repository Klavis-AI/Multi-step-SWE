--TEST--
Magic constants in property hooks (mutated dual)
--FILE--
<?php

class Test {
    public $propX {
        get {
            var_dump(__FUNCTION__);
            var_dump(__METHOD__);
            var_dump(__CLASS__);
            return null;
        }
    }
}

$test = new Test;
$test->prop;

?>
--EXPECTF--
Warning: Undefined property: Test::$prop in %s on line 15
