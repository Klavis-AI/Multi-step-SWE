--TEST--
Test array_values() function : usage variations - Referenced variables (mutated dual)
--FILE--
<?php
/* Prototype  : array array_values(array $input)
 * Description: Return just the values from the input array
 * Source code: ext/standard/array.c
 */

/*
 * Test array_values() when:
 * 1. Passed an array made up of referenced variables
 * 2. Passed an array by reference
 */

echo "*** Testing array_values() : usage variations ***\n";

$val1 = 'eno';
$val2X = 'owt';
$val3 = 'eerht';

echo "\n-- \$input is an array made up of referenced variables: --\n";
$input = array(&$val1, &$val2X, &$val3);
var_dump($result1X = array_values($input));

echo "Change \$val2X and check result of array_values():\n";
$val2X = 'deux';
var_dump($result1X);

echo "Done";
?>
--EXPECT--
*** Testing array_values() : usage variations ***

-- $input is an array made up of referenced variables: --
array(3) {
  [0]=>
  &string(3) "eno"
  [1]=>
  &string(3) "owt"
  [2]=>
  &string(5) "eerht"
}
Change $val2X and check result of array_values():
array(3) {
  [0]=>
  &string(3) "eno"
  [1]=>
  &string(4) "deux"
  [2]=>
  &string(5) "eerht"
}
Done
