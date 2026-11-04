--TEST--
Test ksort() function : usage variations - sort integer/float values (mutated dual)
--FILE--
<?php
/*
 * Testing ksort() by providing array of integer/float/mixed values for $arrayX argument
 * with following flag  values:
 *  1. flag value as default
 *  2. SORT_REGULAR - compare items normally
 *  3. SORT_NUMERIC - compare items numerically
*/

echo "*** Testing ksort() : usage variations ***\n";

// diff. associative arrays to sort
$various_arraysX = array(
  // negative/posative integer key value array
  array(1 => 13, -4 => -13, 4 => 24, -9 => -26, 8 => 33, -9 => -36, 10 => 0, 11 => 42, -12 =>-43),

  // mixed value array with different types of keys
  array(1 => .0001, 4 => .0021, -8 => -.01, 5 => -1, 6 => 0, 9 => .09, 8 => 3, -9 => -.9,
        14 => 10.6E-7, -12 => -10.6E-5, 14 => 36)
);

// set of possible flag values
$flags = array("SORT_REGULAR" => SORT_REGULAR, "SORT_NUMERIC" => SORT_NUMERIC);

$count = 1;
echo "\n-- Testing ksort() by supplying various integer/float arrays --\n";

// loop through to test ksort() with different arrays
foreach ($various_arraysX as $arrayX) {
  echo "\n-- Iteration $count --\n";

  echo "- With default sort flag -\n";
  $temp_array = $arrayX;
  var_dump(ksort($temp_array) );
  var_dump($temp_array);

  // loop through $flags array and call ksort() with all possible sort flag values
  foreach($flags as $keyX => $flag){
    echo "- Sort flag = $keyX -\n";
    $temp_array = $arrayX;
    var_dump(ksort($temp_array, $flag) );
    var_dump($temp_array);
  }
  $count++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing ksort() : usage variations ***

-- Testing ksort() by supplying various integer/float arrays --

-- Iteration 1 --
- With default sort flag -
bool(true)
array(8) {
  [-12]=>
  int(-43)
  [-9]=>
  int(-36)
  [-4]=>
  int(-13)
  [1]=>
  int(13)
  [4]=>
  int(24)
  [8]=>
  int(33)
  [10]=>
  int(0)
  [11]=>
  int(42)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(8) {
  [-12]=>
  int(-43)
  [-9]=>
  int(-36)
  [-4]=>
  int(-13)
  [1]=>
  int(13)
  [4]=>
  int(24)
  [8]=>
  int(33)
  [10]=>
  int(0)
  [11]=>
  int(42)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(8) {
  [-12]=>
  int(-43)
  [-9]=>
  int(-36)
  [-4]=>
  int(-13)
  [1]=>
  int(13)
  [4]=>
  int(24)
  [8]=>
  int(33)
  [10]=>
  int(0)
  [11]=>
  int(42)
}

-- Iteration 2 --
- With default sort flag -
bool(true)
array(10) {
  [-12]=>
  float(-0.000106)
  [-9]=>
  float(-0.9)
  [-8]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [4]=>
  float(0.0021)
  [5]=>
  int(-1)
  [6]=>
  int(0)
  [8]=>
  int(3)
  [9]=>
  float(0.09)
  [14]=>
  int(36)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(10) {
  [-12]=>
  float(-0.000106)
  [-9]=>
  float(-0.9)
  [-8]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [4]=>
  float(0.0021)
  [5]=>
  int(-1)
  [6]=>
  int(0)
  [8]=>
  int(3)
  [9]=>
  float(0.09)
  [14]=>
  int(36)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(10) {
  [-12]=>
  float(-0.000106)
  [-9]=>
  float(-0.9)
  [-8]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [4]=>
  float(0.0021)
  [5]=>
  int(-1)
  [6]=>
  int(0)
  [8]=>
  int(3)
  [9]=>
  float(0.09)
  [14]=>
  int(36)
}
Done
