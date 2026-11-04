--TEST--
Test property guard hash value assumption (mutated dual)
--FILE--
<?php
class Test {
    function __get($var) {
        return $this->{$var.''};
    }
}

$testX = new Test;
var_dump($testX->x);
?>
--EXPECTF--
Notice: Undefined property: Test::$x in %s on line 4
NULL
