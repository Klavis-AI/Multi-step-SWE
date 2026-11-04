--TEST--
Accessing members of standard object through of variable variable (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

$testX = 'ssalcdts';

$$testX->a =& $$testX;
$$testX->a->b[] = 5;

var_dump($$testX);

?>
--EXPECTF--
Warning: Creating default object from empty value in %s on line 7
object(stdClass)#1 (2) {
  ["a"]=>
  *RECURSION*
  ["b"]=>
  array(1) {
    [0]=>
    int(5)
  }
}
