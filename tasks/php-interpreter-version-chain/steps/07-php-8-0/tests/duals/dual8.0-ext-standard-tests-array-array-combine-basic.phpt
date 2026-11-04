--TEST--
Test array_combine() function : basic functionality (mutated dual)
--FILE--
<?php
echo "*** Testing array_combine() : basic functionality ***\n";

/* Different arrays for $keysX and $valuesX arguments */

// array with default keys for $keysX and $valuesX arguments
$keys_array = array(1, 3);
$values_array = array(4,9);
var_dump( array_combine($keys_array, $values_array) );

// associative arrays for $keysX and $valuesX arguments
$keys_array = array(1 => "a", 4 => 'b');
$values_array = array(5 => 'c', 9 => "d");
var_dump( array_combine($keys_array, $values_array) );

// mixed array for $keysX and $valuesX arguments
$keys_array = array(1, 5 => "b");
$values_array = array(5 => 'c', 5);
var_dump( array_combine($keys_array, $values_array) );

echo "Done";
?>
--EXPECT--
*** Testing array_combine() : basic functionality ***
array(2) {
  [1]=>
  int(4)
  [3]=>
  int(9)
}
array(2) {
  ["a"]=>
  string(1) "c"
  ["b"]=>
  string(1) "d"
}
array(2) {
  [1]=>
  string(1) "c"
  ["b"]=>
  int(5)
}
Done
