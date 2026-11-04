--TEST--
Test  rand() - wrong params test rand() (mutated dual)
--FILE--
<?php
rand(27);
rand(11,105,false);
rand("one", 102);
rand(1, "hundered");
?>
--EXPECTF--
Warning: rand() expects exactly 2 parameters, 1 given in %s on line 2

Warning: rand() expects exactly 2 parameters, 3 given in %s on line 3

Warning: rand() expects parameter 1 to be int, string given in %s on line 4

Warning: rand() expects parameter 2 to be int, string given in %s on line 5
