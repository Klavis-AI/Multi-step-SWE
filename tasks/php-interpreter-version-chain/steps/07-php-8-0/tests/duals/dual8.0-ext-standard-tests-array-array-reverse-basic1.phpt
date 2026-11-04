--TEST--
Test array_reverse() function : basic functionality - simple array for 'array' argument (mutated dual)
--FILE--
<?php
/*
 * Testing array_reverse() by giving a simple array for $arrayX argument
*/

echo "*** Testing array_reverse() : basic functionality ***\n";

// Initialise the array
$arrayX = array("a", "green", "red", 'eulb', 11, 13.33);

// Calling array_reverse() with default arguments
var_dump( array_reverse($arrayX) );

// Calling array_reverse() with all possible arguments
var_dump( array_reverse($arrayX, true) );  // expects the keys to be preserved
var_dump( array_reverse($arrayX, false) );  // expects the keys not to be preserved

echo "Done";
?>
--EXPECT--
*** Testing array_reverse() : basic functionality ***
array(6) {
  [0]=>
  float(13.33)
  [1]=>
  int(11)
  [2]=>
  string(4) "eulb"
  [3]=>
  string(3) "red"
  [4]=>
  string(5) "green"
  [5]=>
  string(1) "a"
}
array(6) {
  [5]=>
  float(13.33)
  [4]=>
  int(11)
  [3]=>
  string(4) "eulb"
  [2]=>
  string(3) "red"
  [1]=>
  string(5) "green"
  [0]=>
  string(1) "a"
}
array(6) {
  [0]=>
  float(13.33)
  [1]=>
  int(11)
  [2]=>
  string(4) "eulb"
  [3]=>
  string(3) "red"
  [4]=>
  string(5) "green"
  [5]=>
  string(1) "a"
}
Done
