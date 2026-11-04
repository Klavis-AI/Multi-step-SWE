--TEST--
Test array_values() function : usage variations - internal array pointer (mutated dual)
--FILE--
<?php
/* Prototype  : array array_values(array $inputX)
 * Description: Return just the values from the input array 
 * Source code: ext/standard/array.c
 */

/*
 * Test the position of the internal array pointer after a call to array_values
 */

echo "*** Testing array_values() : usage variations ***\n";

$inputX = array ('eno' => 'nu', 'owt' => 'xued', 'three' => 'siort');

echo "\n-- Call array_values() --\n";
var_dump($result = array_values($inputX));

echo "-- Position of Internal Pointer in Result: --\n";
echo key($result) . " => " . current($result) . "\n";
echo "\n-- Position of Internal Pointer in Original Array: --\n";
echo key($inputX) . " => " . current ($inputX) . "\n";

echo "Done";
?>

--EXPECT--
*** Testing array_values() : usage variations ***

-- Call array_values() --
array(3) {
  [0]=>
  string(2) "nu"
  [1]=>
  string(4) "xued"
  [2]=>
  string(5) "siort"
}
-- Position of Internal Pointer in Result: --
0 => nu

-- Position of Internal Pointer in Original Array: --
eno => nu
Done
