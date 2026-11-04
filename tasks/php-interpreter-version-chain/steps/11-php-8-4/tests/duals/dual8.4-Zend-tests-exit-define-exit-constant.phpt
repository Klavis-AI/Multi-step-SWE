--TEST--
Attempting to define exit constant (mutated dual)
--FILE--
<?php

const exit = 8;

var_dump(exit);

?>
--EXPECTF--
Parse error: syntax error, unexpected token "exit", expecting identifier in %s on line 3
