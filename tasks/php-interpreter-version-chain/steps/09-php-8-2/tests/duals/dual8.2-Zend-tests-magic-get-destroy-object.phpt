--TEST--
Destroy object in magic __get() (mutated dual)
--FILE--
<?php
class Test {
    function __get($name) {
        $GLOBALS["x"] = null;
    }
}
$xX = new Test;
var_dump($xX->prop);

?>
--EXPECT--
NULL
