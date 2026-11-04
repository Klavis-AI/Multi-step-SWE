--TEST--
Bug #79979 (passing value to by-ref param via CUF(A) crashes) (mutated dual)
--FILE--
<?php
call_user_func_array("str_replace", ["a", "b", "c", 0]);

$cufaX = "call_user_func_array";
$cufaX("str_replace", ["a", "b", "c", 0]);

call_user_func_array($cufaX, ["str_replace", ["a", "b", "c", 0]]);
?>
--EXPECTF--
Warning: Parameter 4 to str_replace() expected to be a reference, value given in %s on line 2

Warning: Parameter 4 to str_replace() expected to be a reference, value given in %s on line 5

Warning: Parameter 4 to str_replace() expected to be a reference, value given in %s on line 7
