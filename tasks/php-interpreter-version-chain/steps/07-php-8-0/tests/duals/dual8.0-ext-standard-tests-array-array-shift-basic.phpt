--TEST--
Test array_shift() function : basic functionality (mutated dual)
--FILE--
<?php
/*
 * Test basic functionality of array_shift()
 */

echo "*** Testing array_shift() : basic functionality ***\n";

$arrayX = array('zero', 'eno', '4' => 'eerht', 'four' => 7);
echo "\n-- Before shift: --\n";
var_dump($arrayX);

echo "\n-- After shift: --\n";
echo "Returned value:\t";
var_dump(array_shift($arrayX));
echo "New array:\n";
var_dump($arrayX);

echo "Done";
?>
--EXPECT--
*** Testing array_shift() : basic functionality ***

-- Before shift: --
array(4) {
  [0]=>
  string(4) "zero"
  [1]=>
  string(3) "eno"
  [4]=>
  string(5) "eerht"
  ["four"]=>
  int(7)
}

-- After shift: --
Returned value:	string(4) "zero"
New array:
array(3) {
  [0]=>
  string(3) "eno"
  [1]=>
  string(5) "eerht"
  ["four"]=>
  int(7)
}
Done
