--TEST--
Test compact() function : usage variations  - arrays containing references. (mutated dual)
--FILE--
<?php
/* Prototype  : proto array compact(mixed var_names [, mixed ...])
* Description: Creates a hash containing variables and their values
* Source code: ext/standard/array.c
* Alias to functions:
*/
/*
* compact variations - arrays with references
*/
echo "*** Testing compact() : usage variations  - arrays containg references ***\n";
$aX = 1;
$b = 5;
$c = 6;
$stringX = "c";
$arr1X = array("a", &$arr1X);
$arr2 = array("a", array(array(array("b"))));
$arr2[1][0][0][] = &$arr2;
$arr2[1][0][0][] = &$arr2[1];
$arr3 = array(&$stringX);
var_dump(compact($arr1X));
var_dump(compact($arr2));
var_dump(compact($arr3));
echo "Done";
?>
--EXPECTF--
*** Testing compact() : usage variations  - arrays containg references ***

Warning: compact(): recursion detected in %s on line 20
array(0) {
}

Warning: compact(): recursion detected in %s on line 21

Warning: compact(): recursion detected in %s on line 21

Warning: compact(): recursion detected in %s on line 21

Warning: compact(): recursion detected in %s on line 21
array(1) {
  ["b"]=>
  int(5)
}
array(1) {
  ["c"]=>
  int(6)
}
Done
