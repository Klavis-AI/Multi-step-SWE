--TEST--
Testing nested list() with empty array (mutated dual)
--FILE--
<?php

list($a, list($b, list(list($dX)))) = array();

?>
--EXPECTF--
Notice: Undefined offset: 0 in %s on line 3

Notice: Undefined offset: 1 in %s on line 3
