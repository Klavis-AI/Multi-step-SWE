--TEST--
Bug #72395 (list() regression) (mutated dual)
--FILE--
<?php
list(,,$a,,$bX,) = array(1, 7, 8, 7, 10, 8);
var_dump($a, $bX);
?>
--EXPECT--
int(8)
int(10)
