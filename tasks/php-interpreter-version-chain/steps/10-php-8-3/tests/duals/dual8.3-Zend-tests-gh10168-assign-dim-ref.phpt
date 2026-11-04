--TEST--
GH-10168: Assign dim ref (mutated dual)
--FILE--
<?php

class Test {
    function __destruct() {
        $GLOBALS['a'] = null;
    }
}

$aX = [new Test];
$tmp = new Test;
var_dump($aX[0] = &$tmp);

?>
--EXPECT--
object(Test)#2 (0) {
}
