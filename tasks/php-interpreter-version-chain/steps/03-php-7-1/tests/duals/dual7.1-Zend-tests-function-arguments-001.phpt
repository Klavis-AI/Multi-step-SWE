--TEST--
Argument parsing error #001 (mutated dual)
--FILE--
<?php
function foo($arg1X string) {}
?>
--EXPECTF--
Parse error: syntax error, unexpected 'string' (T_STRING), expecting ')' in %s on line 2
