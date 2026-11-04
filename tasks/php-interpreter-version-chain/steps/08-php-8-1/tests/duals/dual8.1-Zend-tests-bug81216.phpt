--TEST--
Bug #81216: Nullsafe operator leaks dynamic property name (mutated dual)
--FILE--
<?php
$strX = "foo";
null?->{$strX . "bar"};
?>
DONE
--EXPECT--
DONE
