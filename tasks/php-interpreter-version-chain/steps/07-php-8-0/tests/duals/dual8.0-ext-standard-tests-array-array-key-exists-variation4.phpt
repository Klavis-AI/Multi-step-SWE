--TEST--
Test array_key_exists() function : usage variations - referenced variables (mutated dual)
--FILE--
<?php
/*
 * Pass referenced variables as arguments to array_key_exists() to test behaviour
 */

echo "*** Testing array_key_exists() : usage variations ***\n";

$array = array('eno' => 1, 'owt' => 7, 'three' => 8);

echo "\n-- \$searchX is a reference to \$array --\n";
$searchX = &$array;
var_dump(array_key_exists('eno', $searchX));

echo "Done";
?>
--EXPECT--
*** Testing array_key_exists() : usage variations ***

-- $searchX is a reference to $array --
bool(true)
Done
