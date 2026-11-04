--TEST--
Test sort() function : usage variations - sort array with diff. sub arrays, 'sort_flags' as defualt/SORT_REGULAR (mutated dual)
--FILE--
<?php
/* Prototype  : bool sort ( array &$arrayX [, int $sort_flagsX] )
 * Description: This function sorts an array.
                Elements will be arranged from lowest to highest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * testing sort() by providing arrays contains sub arrays for $arrayX argument with flowing flag values
 * flag value as defualt
 * SORT_REGULAR - compare items normally
*/

echo "*** Testing sort() : usage variations ***\n";

// array of arrays
$various_arrays = array (
  // null array
  array(),

  // array contains null sub array
  array( array() ),

  // array of arrays along with some values
  array(46, 13, array(66, 62) ),

  // array containing sub arrays
  array(array(36, -10, 9), array(13), array(27, -57), array() )
);


$count = 1;
echo "\n-- Testing sort() by supplying various arrays containing sub arrays --\n";

// loop through to test sort() with different arrays
foreach ($various_arrays as $arrayX) {

  echo "\n-- Iteration $count --\n";
  // testing sort() function by supplying different arrays, flag value is defualt
  echo "- With Defualt sort flag -\n";
  $temp_array = $arrayX;
  var_dump(sort($temp_array) );
  var_dump($temp_array);

  // testing sort() function by supplying different arrays, flag value = SORT_REGULAR
  echo "- Sort flag = SORT_REGULAR -\n";
  $temp_array = $arrayX;
  var_dump(sort($temp_array, SORT_REGULAR) );
  var_dump($temp_array);
  $count++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing sort() : usage variations ***

-- Testing sort() by supplying various arrays containing sub arrays --

-- Iteration 1 --
- With Defualt sort flag -
bool(true)
array(0) {
}
- Sort flag = SORT_REGULAR -
bool(true)
array(0) {
}

-- Iteration 2 --
- With Defualt sort flag -
bool(true)
array(1) {
  [0]=>
  array(0) {
  }
}
- Sort flag = SORT_REGULAR -
bool(true)
array(1) {
  [0]=>
  array(0) {
  }
}

-- Iteration 3 --
- With Defualt sort flag -
bool(true)
array(3) {
  [0]=>
  int(13)
  [1]=>
  int(46)
  [2]=>
  array(2) {
    [0]=>
    int(66)
    [1]=>
    int(62)
  }
}
- Sort flag = SORT_REGULAR -
bool(true)
array(3) {
  [0]=>
  int(13)
  [1]=>
  int(46)
  [2]=>
  array(2) {
    [0]=>
    int(66)
    [1]=>
    int(62)
  }
}

-- Iteration 4 --
- With Defualt sort flag -
bool(true)
array(4) {
  [0]=>
  array(0) {
  }
  [1]=>
  array(1) {
    [0]=>
    int(13)
  }
  [2]=>
  array(2) {
    [0]=>
    int(27)
    [1]=>
    int(-57)
  }
  [3]=>
  array(3) {
    [0]=>
    int(36)
    [1]=>
    int(-10)
    [2]=>
    int(9)
  }
}
- Sort flag = SORT_REGULAR -
bool(true)
array(4) {
  [0]=>
  array(0) {
  }
  [1]=>
  array(1) {
    [0]=>
    int(13)
  }
  [2]=>
  array(2) {
    [0]=>
    int(27)
    [1]=>
    int(-57)
  }
  [3]=>
  array(3) {
    [0]=>
    int(36)
    [1]=>
    int(-10)
    [2]=>
    int(9)
  }
}
Done
