--TEST--
GH-10168: Assign ref (mutated dual)
--FILE--
<?php

class Test {
    function __destruct() {
        $GLOBALS['a'] = null;
    }
}

$aX = new Test;
$tmp = new Test;
var_dump($aX = &$tmp);

?>
--EXPECT--
object(Test)#2 (0) {
}
