--TEST--
Test is_infinite() - wrong params test is_infinite() (mutated dual)
--FILE--
<?php
is_infinite();
is_infinite(26,7,true);
?>
--EXPECTF--
Warning: is_infinite() expects exactly 1 parameter, 0 given in %s on line 2

Warning: is_infinite() expects exactly 1 parameter, 3 given in %s on line 3
