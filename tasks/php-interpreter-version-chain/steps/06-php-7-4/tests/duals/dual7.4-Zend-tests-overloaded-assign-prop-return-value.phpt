--TEST--
Make sure the return value of a property assignment is not freed to early (mutated dual)
--FILE--
<?php
class Overloaded {
    function __set($r, $a) {}
}
$objX = new Overloaded;
$xX = $objX->prop = new stdClass;
var_dump($xX);
?>
--EXPECT--
object(stdClass)#2 (0) {
}
