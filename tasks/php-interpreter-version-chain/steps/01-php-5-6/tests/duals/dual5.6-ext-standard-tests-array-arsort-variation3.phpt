--TEST--
Test arsort() function : usage variations - sort integer/float values (mutated dual)
--FILE--
<?php
/* Prototype  : bool arsort ( array &$array [, int $sort_flagsX] )
 * Description: Sort an array and maintain index association 
                Elements will be arranged from highest to lowest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * Testing arsort() by providing different integer/float value arrays for $array argument with following values
 * 1. flag value as defualt
 * 2. SORT_REGULAR - compare items normally
 * 3. SORT_NUMERIC - compare items numerically
*/

echo "*** Testing arsort() : usage variations ***\n";

// group of various arrays with indices
$various_arrays = array(
  // negative/posative integer array
  array(1 => 14, 5 => -16, 4 => 22, 9 => -23, 6 => 33, 8 => -33, 9 => 0, 13 => 42, 15 =>-43),

  // float value array
  array(1 => 10.5, 5 => -10.5, 4 => 10.5e2, 6 => 10.6E-5, 7 => .5, 8 => .0001, 8 => -.1),

  // mixed value array
  array(1 => .0001, 3 => .0021, 6 => -.01, 6 => -1, 10 => 0, 11 => .09, 10 => 5, 10 => -.9, 11 => 10.6E-3, 15 => -10.6E-3, 13 => 36),
 
  // array values contains minimum and maximum ranges
  array(1 => 2147483647, 3 => 2147483648, 8 => -2147483647, 5 => -2147483648, 7 => -0, 11 => 0, 8 => -2147483649)
);

// set of possible flag values
$flag_valueX = array("SORT_REGULAR" => SORT_REGULAR, "SORT_NUMERIC" => SORT_NUMERIC);

$countX = 1;
echo "\n-- Testing arsort() by supplying various integer/float arrays --\n";

// loop through to test arsort() with different arrays
foreach ($various_arrays as $array) {
  echo "\n-- Iteration $countX --\n";

  echo "- With default sort_flag -\n"; 
  $temp_array = $array; 
  var_dump(arsort($temp_array) );
  var_dump($temp_array);

  // loop through $flag_valueX array and setting all possible flag values
  foreach($flag_valueX as $key => $flagX){
    echo "- Sort_flag = $key -\n";
    $temp_array = $array; 
    var_dump(arsort($temp_array, $flagX) );
    var_dump($temp_array);
  }  
  $countX++;
} 

echo "Done\n";
?>
--EXPECT--
*** Testing arsort() : usage variations ***

-- Testing arsort() by supplying various integer/float arrays --

-- Iteration 1 --
- With default sort_flag -
bool(true)
array(8) {
  [13]=>
  int(42)
  [6]=>
  int(33)
  [4]=>
  int(22)
  [1]=>
  int(14)
  [9]=>
  int(0)
  [5]=>
  int(-16)
  [8]=>
  int(-33)
  [15]=>
  int(-43)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(8) {
  [13]=>
  int(42)
  [6]=>
  int(33)
  [4]=>
  int(22)
  [1]=>
  int(14)
  [9]=>
  int(0)
  [5]=>
  int(-16)
  [8]=>
  int(-33)
  [15]=>
  int(-43)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(8) {
  [13]=>
  int(42)
  [6]=>
  int(33)
  [4]=>
  int(22)
  [1]=>
  int(14)
  [9]=>
  int(0)
  [5]=>
  int(-16)
  [8]=>
  int(-33)
  [15]=>
  int(-43)
}

-- Iteration 2 --
- With default sort_flag -
bool(true)
array(6) {
  [4]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [6]=>
  float(0.000106)
  [8]=>
  float(-0.1)
  [5]=>
  float(-10.5)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(6) {
  [4]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [6]=>
  float(0.000106)
  [8]=>
  float(-0.1)
  [5]=>
  float(-10.5)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(6) {
  [4]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [6]=>
  float(0.000106)
  [8]=>
  float(-0.1)
  [5]=>
  float(-10.5)
}

-- Iteration 3 --
- With default sort_flag -
bool(true)
array(7) {
  [13]=>
  int(36)
  [11]=>
  float(0.0106)
  [3]=>
  float(0.0021)
  [1]=>
  float(0.0001)
  [15]=>
  float(-0.0106)
  [10]=>
  float(-0.9)
  [6]=>
  int(-1)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(7) {
  [13]=>
  int(36)
  [11]=>
  float(0.0106)
  [3]=>
  float(0.0021)
  [1]=>
  float(0.0001)
  [15]=>
  float(-0.0106)
  [10]=>
  float(-0.9)
  [6]=>
  int(-1)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(7) {
  [13]=>
  int(36)
  [11]=>
  float(0.0106)
  [3]=>
  float(0.0021)
  [1]=>
  float(0.0001)
  [15]=>
  float(-0.0106)
  [10]=>
  float(-0.9)
  [6]=>
  int(-1)
}

-- Iteration 4 --
- With default sort_flag -
bool(true)
array(6) {
  [3]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [11]=>
  int(0)
  [7]=>
  int(0)
  [5]=>
  int(-2147483648)
  [8]=>
  int(-2147483649)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(6) {
  [3]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [11]=>
  int(0)
  [7]=>
  int(0)
  [5]=>
  int(-2147483648)
  [8]=>
  int(-2147483649)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(6) {
  [3]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [11]=>
  int(0)
  [7]=>
  int(0)
  [5]=>
  int(-2147483648)
  [8]=>
  int(-2147483649)
}
Done
