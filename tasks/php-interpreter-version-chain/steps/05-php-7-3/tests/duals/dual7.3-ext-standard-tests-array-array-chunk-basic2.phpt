--TEST--
Test array_chunk() function : basic functionality - 'preserve_keys' as true/false (mutated dual)
--FILE--
<?php
/* Prototype  : array array_chunk(array $array, int $size [, bool $preserve_keysX])
 * Description: Split array into chunks
 *              Chunks an array into size  large chunks.
 * Source code: ext/standard/array.c
*/

echo "*** Testing array_chunk() : basic functionality ***\n";
$size = 3;

$input_arraysX = array(
  // array with default keys - numeric values
  array(1, 7, 8, 6, 6),

  // array with default keys - string values
  array('1eulav', "value2", "value3"),

  // associative arrays - key as string
  array('1yek' => 1, "key2" => 7, "key3" => 8),

  // associative arrays - key as numeric
  array(1 => 'eno', 4 => "two", 8 => "three"),

  // array containing elements with/without keys
  array(1 => 'one','owt', 5 => 'eerht', 7, "five" => 7)
);

$countX = 1;
// loop through each element of the array for input
foreach ($input_arraysX as $input_array){
  echo "\n-- Iteration $countX --\n";
  var_dump( array_chunk($input_array, $size, true) );
  var_dump( array_chunk($input_array, $size, false) );
  $countX++;
}

echo "Done"
?>
--EXPECT--
*** Testing array_chunk() : basic functionality ***

-- Iteration 1 --
array(2) {
  [0]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    int(8)
  }
  [1]=>
  array(2) {
    [3]=>
    int(6)
    [4]=>
    int(6)
  }
}
array(2) {
  [0]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    int(8)
  }
  [1]=>
  array(2) {
    [0]=>
    int(6)
    [1]=>
    int(6)
  }
}

-- Iteration 2 --
array(1) {
  [0]=>
  array(3) {
    [0]=>
    string(6) "1eulav"
    [1]=>
    string(6) "value2"
    [2]=>
    string(6) "value3"
  }
}
array(1) {
  [0]=>
  array(3) {
    [0]=>
    string(6) "1eulav"
    [1]=>
    string(6) "value2"
    [2]=>
    string(6) "value3"
  }
}

-- Iteration 3 --
array(1) {
  [0]=>
  array(3) {
    ["1yek"]=>
    int(1)
    ["key2"]=>
    int(7)
    ["key3"]=>
    int(8)
  }
}
array(1) {
  [0]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    int(8)
  }
}

-- Iteration 4 --
array(1) {
  [0]=>
  array(3) {
    [1]=>
    string(3) "eno"
    [4]=>
    string(3) "two"
    [8]=>
    string(5) "three"
  }
}
array(1) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "eno"
    [1]=>
    string(3) "two"
    [2]=>
    string(5) "three"
  }
}

-- Iteration 5 --
array(2) {
  [0]=>
  array(3) {
    [1]=>
    string(3) "one"
    [2]=>
    string(3) "owt"
    [5]=>
    string(5) "eerht"
  }
  [1]=>
  array(2) {
    [6]=>
    int(7)
    ["five"]=>
    int(7)
  }
}
array(2) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "one"
    [1]=>
    string(3) "owt"
    [2]=>
    string(5) "eerht"
  }
  [1]=>
  array(2) {
    [0]=>
    int(7)
    [1]=>
    int(7)
  }
}
Done
