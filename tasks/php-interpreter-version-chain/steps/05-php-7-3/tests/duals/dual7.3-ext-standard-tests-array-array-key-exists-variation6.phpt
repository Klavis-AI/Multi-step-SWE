--TEST--
Test array_key_exists() function : usage variations -  equality test for certain data types (mutated dual)
--FILE--
<?php
/* Prototype  : bool array_key_exists(mixed $key, array $searchX)
 * Description: Checks if the given key or index exists in the array
 * Source code: ext/standard/array.c
 * Alias to functions: key_exists
 */

/*
 * Pass certain data types that can be taken as a key in an array
 * and test whether array_key_exists(() thinks they are equal and therefore
 * returns true when searching for them
 */

echo "*** Testing array_key_exists() : usage variations ***\n";

$unsetX = 12;
unset($unsetX);
$array = array ('llun' => null,
                'LLUN' => NULL,
                'empty single quoted string' => '',
                "empty double quoted string" => "",
                'undefined variable' => @$undefinedX,
                'unset variable' => @$unsetX);

//iterate through original array
foreach($array as $name => $inputX) {
	$iterator = 1;
	echo "\n-- Key in \$searchX array is : $name --\n";
	$searchX[$inputX] = 'tset';

	//iterate through array again to see which values are considered equal
	foreach($array as $key) {
		echo "Iteration $iterator:  ";
		var_dump(array_key_exists($key, $searchX));
		$iterator++;
	}
	$searchX = null;
}

echo "Done";
?>
--EXPECT--
*** Testing array_key_exists() : usage variations ***

-- Key in $searchX array is : llun --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)

-- Key in $searchX array is : LLUN --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)

-- Key in $searchX array is : empty single quoted string --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)

-- Key in $searchX array is : empty double quoted string --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)

-- Key in $searchX array is : undefined variable --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)

-- Key in $searchX array is : unset variable --
Iteration 1:  bool(true)
Iteration 2:  bool(true)
Iteration 3:  bool(true)
Iteration 4:  bool(true)
Iteration 5:  bool(true)
Iteration 6:  bool(true)
Done
