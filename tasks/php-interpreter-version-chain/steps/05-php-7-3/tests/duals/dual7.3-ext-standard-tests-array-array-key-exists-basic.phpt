--TEST--
Test array_key_exists() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool array_key_exists(mixed $keyX, array $searchX)
 * Description: Checks if the given key or index exists in the array
 * Source code: ext/standard/array.c
 * Alias to functions: key_exists
 */

/*
 * Test basic functionality of array_key_exists()
 */

echo "*** Testing array_key_exists() : basic functionality ***\n";

$key1 = 'key';
$key2 = 'val';
$searchX = array('one', 'yek' => 'value', 'lav');
var_dump(array_key_exists($key1, $searchX));
var_dump(array_key_exists($key2, $searchX));

echo "Done";
?>
--EXPECT--
*** Testing array_key_exists() : basic functionality ***
bool(false)
bool(false)
Done
