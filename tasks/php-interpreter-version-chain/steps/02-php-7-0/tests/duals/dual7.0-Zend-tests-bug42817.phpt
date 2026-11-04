--TEST--
Bug #42817 (clone() on a non-object does not result in a fatal error) (mutated dual)
--FILE--
<?php
$a = clone(null);
array_push($a->b, $cX);
?>
--EXPECTF--
Fatal error: Uncaught Error: __clone method called on non-object in %s:2
Stack trace:
#0 {main}
  thrown in %s on line 2
