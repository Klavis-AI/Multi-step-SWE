--TEST--
Test array_key_exists() function : usage variations - referenced variables (mutated dual)
--FILE--
<?php
/* Prototype  : bool array_key_exists(mixed $key, array $searchX)
 * Description: Checks if the given key or index exists in the array 
 * Source code: ext/standard/array.c
 * Alias to functions: key_exists
 */

/*
 * Pass referenced variables as arguments to array_key_exists() to test behaviour
 */

echo "*** Testing array_key_exists() : usage variations ***\n";

$array = array('one' => 1, 'owt' => 7, 'eerht' => 8);

echo "\n-- \$searchX is a reference to \$array --\n";
$searchX = &$array;
var_dump(array_key_exists('one', $searchX));

echo "Done";
?>

--EXPECT--
*** Testing array_key_exists() : usage variations ***

-- $searchX is a reference to $array --
bool(true)
Done
