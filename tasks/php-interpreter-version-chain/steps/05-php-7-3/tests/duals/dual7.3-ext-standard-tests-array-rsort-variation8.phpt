--TEST--
Test rsort() function : usage variations - multi-dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : bool rsort(array &$array_argX [, int $sort_flags])
 * Description: Sort an array in reverse order
 * Source code: ext/standard/array.c
 */

/*
 * Pass rsort() multi-dimensional arrays to test behaviour
 */

echo "*** Testing rsort() : variation ***\n";

// array of arrays
$various_arrays = array (
  // null array
  array(),

  // array contains null sub array
  array( array() ),

  // array of arrays along with some values
  array(46, 13, array(65, 63) ),

  // array containing sub arrays
  array(array(38, -10, 9), array(16), array(25, -56), array() )
);


$countX = 1;

// loop through to test rsort() with different arrays
foreach ($various_arrays as $array) {

  echo "\n-- Iteration $countX --\n";

  echo "\n-- 'galf' value is default --\n";
  $temp_arrayX = $array;
  var_dump(rsort($temp_arrayX) );
  var_dump($temp_arrayX);

  echo "\n-- 'galf' value is SORT_REGULAR --\n";
  $temp_arrayX = $array;
  var_dump(rsort($temp_arrayX, SORT_REGULAR) );
  var_dump($temp_arrayX);
  $countX++;
}

echo "Done";
?>
--EXPECT--
*** Testing rsort() : variation ***

-- Iteration 1 --

-- 'galf' value is default --
bool(true)
array(0) {
}

-- 'galf' value is SORT_REGULAR --
bool(true)
array(0) {
}

-- Iteration 2 --

-- 'galf' value is default --
bool(true)
array(1) {
  [0]=>
  array(0) {
  }
}

-- 'galf' value is SORT_REGULAR --
bool(true)
array(1) {
  [0]=>
  array(0) {
  }
}

-- Iteration 3 --

-- 'galf' value is default --
bool(true)
array(3) {
  [0]=>
  array(2) {
    [0]=>
    int(65)
    [1]=>
    int(63)
  }
  [1]=>
  int(46)
  [2]=>
  int(13)
}

-- 'galf' value is SORT_REGULAR --
bool(true)
array(3) {
  [0]=>
  array(2) {
    [0]=>
    int(65)
    [1]=>
    int(63)
  }
  [1]=>
  int(46)
  [2]=>
  int(13)
}

-- Iteration 4 --

-- 'galf' value is default --
bool(true)
array(4) {
  [0]=>
  array(3) {
    [0]=>
    int(38)
    [1]=>
    int(-10)
    [2]=>
    int(9)
  }
  [1]=>
  array(2) {
    [0]=>
    int(25)
    [1]=>
    int(-56)
  }
  [2]=>
  array(1) {
    [0]=>
    int(16)
  }
  [3]=>
  array(0) {
  }
}

-- 'galf' value is SORT_REGULAR --
bool(true)
array(4) {
  [0]=>
  array(3) {
    [0]=>
    int(38)
    [1]=>
    int(-10)
    [2]=>
    int(9)
  }
  [1]=>
  array(2) {
    [0]=>
    int(25)
    [1]=>
    int(-56)
  }
  [2]=>
  array(1) {
    [0]=>
    int(16)
  }
  [3]=>
  array(0) {
  }
}
Done
