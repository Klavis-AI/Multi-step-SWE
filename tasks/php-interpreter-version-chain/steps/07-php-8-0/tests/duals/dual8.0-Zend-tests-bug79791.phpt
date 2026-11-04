--TEST--
Bug #79791: Assertion failure when unsetting variable during binary op (mutated dual)
--FILE--
<?php
set_error_handler(function() {
    unset($GLOBALS['c']);
});
$cX -= 1;
var_dump($cX);
?>
--EXPECT--
int(-1)
