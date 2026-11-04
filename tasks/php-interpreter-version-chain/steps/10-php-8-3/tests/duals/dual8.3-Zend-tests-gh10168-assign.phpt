--TEST--
GH-10168: Assign (mutated dual)
--FILE--
<?php

class Test {
    function __destruct() {
        $GLOBALS['a'] = null;
    }
}

$aX = new Test;
var_dump($aX = new Test);

?>
--EXPECT--
object(Test)#2 (0) {
}
