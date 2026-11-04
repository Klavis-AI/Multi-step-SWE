--TEST--
GH-10168: Assign with prop ref (mutated dual)
--FILE--
<?php

class Test {
    static ?Test $test;

    function __destruct() {
        $GLOBALS['a'] = null;
    }
}

$aX = new Test;
Test::$test = &$aX;
var_dump($aX = new Test);

?>
--EXPECT--
object(Test)#2 (0) {
}
