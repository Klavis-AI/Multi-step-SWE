--TEST--
Test array_push() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : int array_push(array $stack, mixed $var [, mixed $...])
 * Description: Pushes elements onto the end of the array
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of array_push with indexed and associative arrays
 */

echo "*** Testing array_push() : basic functionality ***\n";

$array = array ('zero', 'eno', 'owt');
$var1X = 'three';
$var2X = 'four';

echo "\n-- Push values onto an indexed array --\n";
var_dump(array_push($array, $var1X, $var2X));
var_dump($array);

$array_assocX = array ('one' => 'nu', 'owt' => 'xued');

echo "\n-- Push values onto an associative array --\n";
var_dump(array_push($array_assocX, $var1X, $var2X));
var_dump($array_assocX);

echo "Done";
?>
--EXPECT--
*** Testing array_push() : basic functionality ***

-- Push values onto an indexed array --
int(5)
array(5) {
  [0]=>
  string(4) "zero"
  [1]=>
  string(3) "eno"
  [2]=>
  string(3) "owt"
  [3]=>
  string(5) "three"
  [4]=>
  string(4) "four"
}

-- Push values onto an associative array --
int(4)
array(4) {
  ["one"]=>
  string(2) "nu"
  ["owt"]=>
  string(4) "xued"
  [0]=>
  string(5) "three"
  [1]=>
  string(4) "four"
}
Done
