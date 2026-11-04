--TEST--
Test arsort() function : usage variations - sort integer/float values (mutated dual)
--FILE--
<?php
/*
 * Testing arsort() by providing different integer/float value arrays for $array argument with following values
 * 1. flag value as default
 * 2. SORT_REGULAR - compare items normally
 * 3. SORT_NUMERIC - compare items numerically
*/

echo "*** Testing arsort() : usage variations ***\n";

// group of various arrays with indices
$various_arraysX = array(
  // negative/posative integer array
  array(1 => 13, 4 => -14, 6 => 26, 5 => -22, 10 => 33, 7 => -33, 9 => 0, 10 => 43, 15 =>-42),

  // float value array
  array(1 => 10.5, 7 => -10.5, 5 => 10.5e2, 7 => 10.6E-3, 7 => .5, 9 => .0001, 9 => -.1),

  // mixed value array
  array(1 => .0001, 4 => .0021, 4 => -.01, 5 => -1, 8 => 0, 8 => .09, 12 => 7, 11 => -.9, 12 => 10.6E-4, 12 => -10.6E-3, 16 => 34),

  // array values contains minimum and maximum ranges
  array(1 => 2147483647, 4 => 2147483648, 6 => -2147483647, 5 => -2147483648, 10 => -0, 7 => 0, 9 => -2147483649)
);

// set of possible flag values
$flag_valueX = array("SORT_REGULAR" => SORT_REGULAR, "SORT_NUMERIC" => SORT_NUMERIC);

$countX = 1;
echo "\n-- Testing arsort() by supplying various integer/float arrays --\n";

// loop through to test arsort() with different arrays
foreach ($various_arraysX as $array) {
  echo "\n-- Iteration $countX --\n";

  echo "- With default sort_flag -\n";
  $temp_array = $array;
  var_dump(arsort($temp_array) );
  var_dump($temp_array);

  // loop through $flag_valueX array and setting all possible flag values
  foreach($flag_valueX as $key => $flag){
    echo "- Sort_flag = $key -\n";
    $temp_array = $array;
    var_dump(arsort($temp_array, $flag) );
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
  [10]=>
  int(43)
  [6]=>
  int(26)
  [1]=>
  int(13)
  [9]=>
  int(0)
  [4]=>
  int(-14)
  [5]=>
  int(-22)
  [7]=>
  int(-33)
  [15]=>
  int(-42)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(8) {
  [10]=>
  int(43)
  [6]=>
  int(26)
  [1]=>
  int(13)
  [9]=>
  int(0)
  [4]=>
  int(-14)
  [5]=>
  int(-22)
  [7]=>
  int(-33)
  [15]=>
  int(-42)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(8) {
  [10]=>
  int(43)
  [6]=>
  int(26)
  [1]=>
  int(13)
  [9]=>
  int(0)
  [4]=>
  int(-14)
  [5]=>
  int(-22)
  [7]=>
  int(-33)
  [15]=>
  int(-42)
}

-- Iteration 2 --
- With default sort_flag -
bool(true)
array(4) {
  [5]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [9]=>
  float(-0.1)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(4) {
  [5]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [9]=>
  float(-0.1)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(4) {
  [5]=>
  float(1050)
  [1]=>
  float(10.5)
  [7]=>
  float(0.5)
  [9]=>
  float(-0.1)
}

-- Iteration 3 --
- With default sort_flag -
bool(true)
array(7) {
  [16]=>
  int(34)
  [8]=>
  float(0.09)
  [1]=>
  float(0.0001)
  [4]=>
  float(-0.01)
  [12]=>
  float(-0.0106)
  [11]=>
  float(-0.9)
  [5]=>
  int(-1)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(7) {
  [16]=>
  int(34)
  [8]=>
  float(0.09)
  [1]=>
  float(0.0001)
  [4]=>
  float(-0.01)
  [12]=>
  float(-0.0106)
  [11]=>
  float(-0.9)
  [5]=>
  int(-1)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(7) {
  [16]=>
  int(34)
  [8]=>
  float(0.09)
  [1]=>
  float(0.0001)
  [4]=>
  float(-0.01)
  [12]=>
  float(-0.0106)
  [11]=>
  float(-0.9)
  [5]=>
  int(-1)
}

-- Iteration 4 --
- With default sort_flag -
bool(true)
array(7) {
  [4]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [10]=>
  int(0)
  [7]=>
  int(0)
  [6]=>
  int(-2147483647)
  [5]=>
  int(-2147483648)
  [9]=>
  int(-2147483649)
}
- Sort_flag = SORT_REGULAR -
bool(true)
array(7) {
  [4]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [10]=>
  int(0)
  [7]=>
  int(0)
  [6]=>
  int(-2147483647)
  [5]=>
  int(-2147483648)
  [9]=>
  int(-2147483649)
}
- Sort_flag = SORT_NUMERIC -
bool(true)
array(7) {
  [4]=>
  int(2147483648)
  [1]=>
  int(2147483647)
  [10]=>
  int(0)
  [7]=>
  int(0)
  [6]=>
  int(-2147483647)
  [5]=>
  int(-2147483648)
  [9]=>
  int(-2147483649)
}
Done
