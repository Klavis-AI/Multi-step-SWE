--TEST--
Test var_export() function : error conditions - recursive array (mutated dual)
--FILE--
<?php
/* Prototype  : mixed var_export(mixed var [, bool return])
 * Description: Outputs or returns a string representation of a variable
 * Source code: ext/standard/var.c
 * Alias to functions:
 */

$aX[] =& $aX;
var_export($aX, true);

?>
===DONE===
--EXPECTF--
Warning: var_export does not handle circular references in %s on line 9
===DONE===
