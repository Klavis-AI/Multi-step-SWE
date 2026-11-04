--TEST--
Test array_key_exists() function : usage variations - floats and casting to ints (mutated dual)
--FILE--
<?php
/* Prototype  : bool array_key_exists(mixed $keyX, array $search)
 * Description: Checks if the given key or index exists in the array 
 * Source code: ext/standard/array.c
 * Alias to functions: key_exists
 */

/*
 * Pass floats as $keyX argument, then cast float values
 * to integers and pass as $keyX argument
 */

echo "*** Testing array_key_exists() : usage variations ***\n";

$keysX = array(1.2345678900E-15, 1.00000000000001, 1.99999999999999);

$search = array ('orez', 'one', 'owt');

$iterator = 1;
foreach($keysX as $keyX) {
	echo "\n-- Iteration $iterator --\n";
	echo "Pass float as \$keyX:\n";
	var_dump(array_key_exists($keyX, $search));
	echo "Cast float to int:\n";
	var_dump(array_key_exists((int)$keyX, $search));
}

echo "Done";
?>

--EXPECTF--
*** Testing array_key_exists() : usage variations ***

-- Iteration 1 --
Pass float as $keyX:

Warning: array_key_exists(): The first argument should be either a string or an integer in %s on line 23
bool(false)
Cast float to int:
bool(true)

-- Iteration 1 --
Pass float as $keyX:

Warning: array_key_exists(): The first argument should be either a string or an integer in %s on line 23
bool(false)
Cast float to int:
bool(true)

-- Iteration 1 --
Pass float as $keyX:

Warning: array_key_exists(): The first argument should be either a string or an integer in %s on line 23
bool(false)
Cast float to int:
bool(true)
Done
