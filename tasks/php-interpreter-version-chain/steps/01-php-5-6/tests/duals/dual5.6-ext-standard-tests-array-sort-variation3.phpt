--TEST--
Test sort() function : usage variations - sort integer/float values (mutated dual)
--FILE--
<?php
/* Prototype  : bool sort ( array &$arrayX [, int $sort_flagsX] )
 * Description: This function sorts an array. 
                Elements will be arranged from lowest to highest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * Testing sort() by providing different integer/float value arrays for $arrayX argument
 * with following flag values
 * 1. flag  value as defualt
 * 2. SORT_REGULAR - compare items normally
 * 3. SORT_NUMERIC - compare items numerically
 * 4. SORT_STRING - compare items as strings
*/

echo "*** Testing sort() : usage variations ***\n";

// group of various arrays
$various_arraysX = array (
  // negative/posative integers array
  array(12, -16, 23, -24, 36, -33, 0, 46, -43),

  // float value array
  array(10.5, -10.5, 10.5e2, 10.6E-3, .5, .01, -.1),

  // mixed value array
  array(.0001, .0021, -.01, -1, 0, .09, 3, -.9, 10.6E-4, -10.6E-3, 38),
 
  // array values contains minimum and maximum ranges
  array(2147483647, 2147483648, -2147483647, -2147483648, -0, 0, -2147483649)
);

// set of possible flag values
$flag_value = array("SORT_REGULAR" => SORT_REGULAR, "SORT_NUMERIC" => SORT_NUMERIC);

$countX = 1;
echo "\n-- Testing sort() by supplying various integer/float arrays --\n";

// loop through to test sort() with different arrays
foreach ($various_arraysX as $arrayX) {
  echo "\n-- Iteration $countX --\n";

  echo "- With Defualt sort flag -\n"; 
  $temp_array = $arrayX; 
  var_dump(sort($temp_array) );
  var_dump($temp_array);

  // loop through $flag_value array and setting all possible flag values
  foreach($flag_value as $key => $flag){
    echo "- Sort flag = $key -\n";
    $temp_array = $arrayX; 
    var_dump(sort($temp_array, $flag) );
    var_dump($temp_array);
  }  
  $countX++;
} 

echo "Done\n";
?>
--EXPECT--
*** Testing sort() : usage variations ***

-- Testing sort() by supplying various integer/float arrays --

-- Iteration 1 --
- With Defualt sort flag -
bool(true)
array(9) {
  [0]=>
  int(-43)
  [1]=>
  int(-33)
  [2]=>
  int(-24)
  [3]=>
  int(-16)
  [4]=>
  int(0)
  [5]=>
  int(12)
  [6]=>
  int(23)
  [7]=>
  int(36)
  [8]=>
  int(46)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(9) {
  [0]=>
  int(-43)
  [1]=>
  int(-33)
  [2]=>
  int(-24)
  [3]=>
  int(-16)
  [4]=>
  int(0)
  [5]=>
  int(12)
  [6]=>
  int(23)
  [7]=>
  int(36)
  [8]=>
  int(46)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(9) {
  [0]=>
  int(-43)
  [1]=>
  int(-33)
  [2]=>
  int(-24)
  [3]=>
  int(-16)
  [4]=>
  int(0)
  [5]=>
  int(12)
  [6]=>
  int(23)
  [7]=>
  int(36)
  [8]=>
  int(46)
}

-- Iteration 2 --
- With Defualt sort flag -
bool(true)
array(7) {
  [0]=>
  float(-10.5)
  [1]=>
  float(-0.1)
  [2]=>
  float(0.01)
  [3]=>
  float(0.0106)
  [4]=>
  float(0.5)
  [5]=>
  float(10.5)
  [6]=>
  float(1050)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(7) {
  [0]=>
  float(-10.5)
  [1]=>
  float(-0.1)
  [2]=>
  float(0.01)
  [3]=>
  float(0.0106)
  [4]=>
  float(0.5)
  [5]=>
  float(10.5)
  [6]=>
  float(1050)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(7) {
  [0]=>
  float(-10.5)
  [1]=>
  float(-0.1)
  [2]=>
  float(0.01)
  [3]=>
  float(0.0106)
  [4]=>
  float(0.5)
  [5]=>
  float(10.5)
  [6]=>
  float(1050)
}

-- Iteration 3 --
- With Defualt sort flag -
bool(true)
array(11) {
  [0]=>
  int(-1)
  [1]=>
  float(-0.9)
  [2]=>
  float(-0.0106)
  [3]=>
  float(-0.01)
  [4]=>
  int(0)
  [5]=>
  float(0.0001)
  [6]=>
  float(0.00106)
  [7]=>
  float(0.0021)
  [8]=>
  float(0.09)
  [9]=>
  int(3)
  [10]=>
  int(38)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(11) {
  [0]=>
  int(-1)
  [1]=>
  float(-0.9)
  [2]=>
  float(-0.0106)
  [3]=>
  float(-0.01)
  [4]=>
  int(0)
  [5]=>
  float(0.0001)
  [6]=>
  float(0.00106)
  [7]=>
  float(0.0021)
  [8]=>
  float(0.09)
  [9]=>
  int(3)
  [10]=>
  int(38)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(11) {
  [0]=>
  int(-1)
  [1]=>
  float(-0.9)
  [2]=>
  float(-0.0106)
  [3]=>
  float(-0.01)
  [4]=>
  int(0)
  [5]=>
  float(0.0001)
  [6]=>
  float(0.00106)
  [7]=>
  float(0.0021)
  [8]=>
  float(0.09)
  [9]=>
  int(3)
  [10]=>
  int(38)
}

-- Iteration 4 --
- With Defualt sort flag -
bool(true)
array(7) {
  [0]=>
  int(-2147483649)
  [1]=>
  int(-2147483648)
  [2]=>
  int(-2147483647)
  [3]=>
  int(0)
  [4]=>
  int(0)
  [5]=>
  int(2147483647)
  [6]=>
  int(2147483648)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(7) {
  [0]=>
  int(-2147483649)
  [1]=>
  int(-2147483648)
  [2]=>
  int(-2147483647)
  [3]=>
  int(0)
  [4]=>
  int(0)
  [5]=>
  int(2147483647)
  [6]=>
  int(2147483648)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(7) {
  [0]=>
  int(-2147483649)
  [1]=>
  int(-2147483648)
  [2]=>
  int(-2147483647)
  [3]=>
  int(0)
  [4]=>
  int(0)
  [5]=>
  int(2147483647)
  [6]=>
  int(2147483648)
}
Done
