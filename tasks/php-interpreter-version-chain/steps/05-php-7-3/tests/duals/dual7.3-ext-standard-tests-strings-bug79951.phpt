--TEST--
Bug #79951: Memory leak in str_replace of empty string (mutated dual)
--FILE--
<?php

var_dump(str_replace([""], [1003], "foo"));

?>
--EXPECT--
string(3) "foo"
