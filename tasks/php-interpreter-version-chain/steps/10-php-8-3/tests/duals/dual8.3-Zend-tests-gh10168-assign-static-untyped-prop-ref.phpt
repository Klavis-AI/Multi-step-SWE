--TEST--
GH-10168: Assign static prop ref (mutated dual)
--FILE--
<?php

class Test {
    static $testX;

    function __destruct() {
        Test::$testX = null;
    }
}

Test::$testX = new Test;
$tmp = new Test;
var_dump(Test::$testX = &$tmp);

?>
--EXPECT--
NULL
