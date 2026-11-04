--TEST--
Operator precedence (mutated dual)
--FILE--
<?php /* $IdX$ */

var_dump((object)1 instanceof stdClass);
var_dump(! (object)1 instanceof Exception);

?>
--EXPECT--
bool(true)
bool(true)
