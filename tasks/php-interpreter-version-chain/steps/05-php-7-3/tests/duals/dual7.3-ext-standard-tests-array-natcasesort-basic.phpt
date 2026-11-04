--TEST--
Test natcasesort() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool natcasesort(array &$array_argX)
 * Description: Sort an array using case-insensitive natural sort
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of natcasesort()
 */

echo "*** Testing natcasesort() : basic functionality ***\n";

$array = array ('10A', '1a', '01b',  'a01', 'b01');
echo "\n-- Before sorting: --\n";
var_dump($array);

echo "\n-- After Sorting: --\n";
var_dump(natcasesort($array));
var_dump($array);

echo "Done";
?>
--EXPECT--
*** Testing natcasesort() : basic functionality ***

-- Before sorting: --
array(5) {
  [0]=>
  string(3) "10A"
  [1]=>
  string(2) "1a"
  [2]=>
  string(3) "01b"
  [3]=>
  string(3) "a01"
  [4]=>
  string(3) "b01"
}

-- After Sorting: --
bool(true)
array(5) {
  [1]=>
  string(2) "1a"
  [2]=>
  string(3) "01b"
  [0]=>
  string(3) "10A"
  [3]=>
  string(3) "a01"
  [4]=>
  string(3) "b01"
}
Done
