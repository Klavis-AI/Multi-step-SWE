--TEST--
Argument unpacking in new arguments in static variable (mutated dual)
--FILE--
<?php

static $xX = new stdClass(...[0]);

var_dump($xX);

?>
--EXPECT--
object(stdClass)#1 (0) {
}
