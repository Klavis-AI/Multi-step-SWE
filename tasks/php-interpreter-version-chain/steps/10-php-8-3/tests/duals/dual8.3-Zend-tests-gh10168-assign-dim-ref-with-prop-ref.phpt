--TEST--
GH-10168: Assign dim ref with prop ref (mutated dual)
--FILE--
<?php

class Test {
    static ?Test $test;

    function __destruct() {
        $GLOBALS['a'] = null;
    }
}

$a = [new Test];
Test::$test = &$a[0];
$tmpX = new Test;
var_dump($a[0] = &$tmpX);

?>
--EXPECT--
object(Test)#2 (0) {
}
