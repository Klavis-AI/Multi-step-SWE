--TEST--
Test ksort() function : usage variations - sort integer/float values (mutated dual)
--FILE--
<?php
/* Prototype  : bool ksort ( array &$arrayX [, int $sort_flags] )
 * Description: Sort an array by key, maintaining key to data correlation
 * Source code: ext/standard/array.c
*/

/*
 * Testing ksort() by providing array of integer/float/mixed values for $arrayX argument
 * with following flag  values:
 *  1. flag value as defualt
 *  2. SORT_REGULAR - compare items normally
 *  3. SORT_NUMERIC - compare items numerically
*/

echo "*** Testing ksort() : usage variations ***\n";

// diff. associative arrays to sort
$various_arraysX = array(
  // negative/posative integer key value array
  array(1 => 13, -4 => -12, 6 => 26, -9 => -24, 7 => 34, -11 => -34, 10 => 0, 9 => 43, -12 =>-43),

  // float key values
  array(1.0 => 10.5, 0.2 => -10.5, 3.1 => 10.5e2, 9 => 10.6E-3, .5 => .5, 7 => .0001, -10 => -.1),

  // mixed value array with different types of keys
  array(1 => .0001, 3 => .0021, -4 => -.01, 5 => -1, 10 => 0, 11 => .09, 9 => 5, -11 => -.9,
        12 => 10.6E-7, -15 => -10.6E-3, 16 => 36)
);

// set of possible flag values
$flags = array("SORT_REGULAR" => SORT_REGULAR, "SORT_NUMERIC" => SORT_NUMERIC);

$countX = 1;
echo "\n-- Testing ksort() by supplying various integer/float arrays --\n";

// loop through to test ksort() with different arrays
foreach ($various_arraysX as $arrayX) {
  echo "\n-- Iteration $countX --\n";

  echo "- With defualt sort flag -\n";
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
  $countX++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing ksort() : usage variations ***

-- Testing ksort() by supplying various integer/float arrays --

-- Iteration 1 --
- With defualt sort flag -
bool(true)
array(9) {
  [-12]=>
  int(-43)
  [-11]=>
  int(-34)
  [-9]=>
  int(-24)
  [-4]=>
  int(-12)
  [1]=>
  int(13)
  [6]=>
  int(26)
  [7]=>
  int(34)
  [9]=>
  int(43)
  [10]=>
  int(0)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(9) {
  [-12]=>
  int(-43)
  [-11]=>
  int(-34)
  [-9]=>
  int(-24)
  [-4]=>
  int(-12)
  [1]=>
  int(13)
  [6]=>
  int(26)
  [7]=>
  int(34)
  [9]=>
  int(43)
  [10]=>
  int(0)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(9) {
  [-12]=>
  int(-43)
  [-11]=>
  int(-34)
  [-9]=>
  int(-24)
  [-4]=>
  int(-12)
  [1]=>
  int(13)
  [6]=>
  int(26)
  [7]=>
  int(34)
  [9]=>
  int(43)
  [10]=>
  int(0)
}

-- Iteration 2 --
- With defualt sort flag -
bool(true)
array(6) {
  [-10]=>
  float(-0.1)
  [0]=>
  float(0.5)
  [1]=>
  float(10.5)
  [3]=>
  float(1050)
  [7]=>
  float(0.0001)
  [9]=>
  float(0.0106)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(6) {
  [-10]=>
  float(-0.1)
  [0]=>
  float(0.5)
  [1]=>
  float(10.5)
  [3]=>
  float(1050)
  [7]=>
  float(0.0001)
  [9]=>
  float(0.0106)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(6) {
  [-10]=>
  float(-0.1)
  [0]=>
  float(0.5)
  [1]=>
  float(10.5)
  [3]=>
  float(1050)
  [7]=>
  float(0.0001)
  [9]=>
  float(0.0106)
}

-- Iteration 3 --
- With defualt sort flag -
bool(true)
array(11) {
  [-15]=>
  float(-0.0106)
  [-11]=>
  float(-0.9)
  [-4]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [3]=>
  float(0.0021)
  [5]=>
  int(-1)
  [9]=>
  int(5)
  [10]=>
  int(0)
  [11]=>
  float(0.09)
  [12]=>
  float(1.06E-6)
  [16]=>
  int(36)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(11) {
  [-15]=>
  float(-0.0106)
  [-11]=>
  float(-0.9)
  [-4]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [3]=>
  float(0.0021)
  [5]=>
  int(-1)
  [9]=>
  int(5)
  [10]=>
  int(0)
  [11]=>
  float(0.09)
  [12]=>
  float(1.06E-6)
  [16]=>
  int(36)
}
- Sort flag = SORT_NUMERIC -
bool(true)
array(11) {
  [-15]=>
  float(-0.0106)
  [-11]=>
  float(-0.9)
  [-4]=>
  float(-0.01)
  [1]=>
  float(0.0001)
  [3]=>
  float(0.0021)
  [5]=>
  int(-1)
  [9]=>
  int(5)
  [10]=>
  int(0)
  [11]=>
  float(0.09)
  [12]=>
  float(1.06E-6)
  [16]=>
  int(36)
}
Done
