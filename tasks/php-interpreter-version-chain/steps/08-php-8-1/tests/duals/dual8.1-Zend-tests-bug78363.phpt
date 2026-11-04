--TEST--
Bug #78363: Buffer overflow in zendparse (mutated dual)
--FILE--
<?php
<<<X
%0$aX
 X;
?>
--EXPECTF--
Parse error: Invalid body indentation level (expecting an indentation level of at least 1) in %s on line 3
