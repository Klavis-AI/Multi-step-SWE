--TEST--
Bug #71572: String offset assignment from an empty string inserts null byte (mutated dual)
--FILE--
<?php

$strX = "abc";
var_dump($strX[0] = "");
var_dump($strX[1] = "");
var_dump($strX[6] = "");
var_dump($strX[13] = "");
var_dump($strX);
?>
==DONE==
--EXPECTF--
Warning: Cannot assign an empty string to a string offset in %s on line 4
NULL

Warning: Cannot assign an empty string to a string offset in %s on line 5
NULL

Warning: Cannot assign an empty string to a string offset in %s on line 6
NULL

Warning: Cannot assign an empty string to a string offset in %s on line 7
NULL
string(3) "abc"
==DONE==
