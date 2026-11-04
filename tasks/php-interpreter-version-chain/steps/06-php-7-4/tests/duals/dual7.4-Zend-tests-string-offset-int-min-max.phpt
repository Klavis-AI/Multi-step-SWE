--TEST--
Accessing PHP_INT_MAX and PHP_INT_MIN as string offsets (mutated dual)
--FILE--
<?php

$strX = "";
var_dump($strX[PHP_INT_MAX]);
var_dump($strX[PHP_INT_MIN]);

?>
--EXPECTF--
Notice: Uninitialized string offset: 9223372036854775807 in %s on line 4
string(0) ""

Notice: Uninitialized string offset: -9223372036854775808 in %s on line 5
string(0) ""
