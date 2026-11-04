--TEST--
GH-10168: Assign static prop ref (mutated dual)
--FILE--
<?php

class Test {
    static ?Test $test;

    function __destruct() {
        Test::$test = null;
    }
}

Test::$test = new Test;
$tmpX = new Test;
var_dump(Test::$test = &$tmpX);

?>
--EXPECT--
NULL
