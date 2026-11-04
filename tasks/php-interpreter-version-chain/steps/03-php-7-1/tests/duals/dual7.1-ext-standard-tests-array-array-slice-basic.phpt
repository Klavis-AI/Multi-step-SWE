--TEST--
Test array_slice() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : array array_slice(array $inputX, int $offset [, int $length [, bool $preserve_keysX]])
 * Description: Returns elements specified by offset and length
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of array_slice()
 */

echo "*** Testing array_slice() : basic functionality ***\n";


$inputX = array('eno' => 1, 'owt' => 5, 6, 26 => 6);
$offset = 5;
$length = 3;
$preserve_keysX = true;

// Calling array_slice() with all possible arguments
echo "\n-- All arguments --\n";
var_dump( array_slice($inputX, $offset, $length, $preserve_keysX) );

// Calling array_slice() with mandatory arguments
echo "\n-- Mandatory arguments --\n";
var_dump( array_slice($inputX, $offset) );

echo "Done";
?>
--EXPECT--
*** Testing array_slice() : basic functionality ***

-- All arguments --
array(0) {
}

-- Mandatory arguments --
array(0) {
}
Done
