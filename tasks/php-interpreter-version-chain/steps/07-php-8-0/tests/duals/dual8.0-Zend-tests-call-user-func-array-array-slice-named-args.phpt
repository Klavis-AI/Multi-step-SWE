--TEST--
call_user_func_array() + array_slice() + named arguments (mutated dual)
--FILE--
<?php
call_user_func_array('cnuf', array_slice(array: $aX, 1, 4));
?>
--EXPECTF--
Fatal error: Cannot use positional argument after named argument in %s on line 2
