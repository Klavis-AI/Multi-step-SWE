--TEST--
Test ksort() function : usage variations - sorting arrays with/without keys (mutated dual)
--FILE--
<?php
/*
 * Testing ksort() by providing arrays  with/without key values for $arrayX argument with following flag values:
 *  1.flag value as default
 *  2.SORT_REGULAR - compare items normally
 */

echo "*** Testing ksort() : usage variations ***\n";

// list of arrays with/without key values
$various_arrays = array (
  array(6 => 56,  69, 25, 34, 16),
  array ("a" => "orange",  "banana", "c" => "apple"),
  array(1, 7, 4, 5, 7, 9),
  array("first", 7 => "second", 1 => "third"),
  array(1, 1, 9 => 1,  7 => 1, 20, 8 => 14),
  array('rab' => 'zab', "foo" => 1),
  array('a' => 1,'b' => array('e' => 3,'f' => 8),'c' => array('g' => 6),'d' => 8),
);

$countX = 1;
echo "\n-- Testing ksort() by supplying various arrays with/without key values --\n";

// loop through to test ksort() with different arrays,
foreach ($various_arrays as $arrayX) {
  echo "\n-- Iteration $countX --\n";

  echo "- With default sort flag -\n";
  $temp_array = $arrayX;
  var_dump( ksort($temp_array) );
  var_dump($temp_array);

  echo "- Sort flag = SORT_REGULAR -\n";
  $temp_array = $arrayX;
  var_dump( ksort($temp_array, SORT_REGULAR) );
  var_dump($temp_array);
  $countX++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing ksort() : usage variations ***

-- Testing ksort() by supplying various arrays with/without key values --

-- Iteration 1 --
- With default sort flag -
bool(true)
array(5) {
  [6]=>
  int(56)
  [7]=>
  int(69)
  [8]=>
  int(25)
  [9]=>
  int(34)
  [10]=>
  int(16)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(5) {
  [6]=>
  int(56)
  [7]=>
  int(69)
  [8]=>
  int(25)
  [9]=>
  int(34)
  [10]=>
  int(16)
}

-- Iteration 2 --
- With default sort flag -
bool(true)
array(3) {
  [0]=>
  string(6) "banana"
  ["a"]=>
  string(6) "orange"
  ["c"]=>
  string(5) "apple"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(3) {
  [0]=>
  string(6) "banana"
  ["a"]=>
  string(6) "orange"
  ["c"]=>
  string(5) "apple"
}

-- Iteration 3 --
- With default sort flag -
bool(true)
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(4)
  [3]=>
  int(5)
  [4]=>
  int(7)
  [5]=>
  int(9)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(4)
  [3]=>
  int(5)
  [4]=>
  int(7)
  [5]=>
  int(9)
}

-- Iteration 4 --
- With default sort flag -
bool(true)
array(3) {
  [0]=>
  string(5) "first"
  [1]=>
  string(5) "third"
  [7]=>
  string(6) "second"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(3) {
  [0]=>
  string(5) "first"
  [1]=>
  string(5) "third"
  [7]=>
  string(6) "second"
}

-- Iteration 5 --
- With default sort flag -
bool(true)
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [7]=>
  int(1)
  [8]=>
  int(14)
  [9]=>
  int(1)
  [10]=>
  int(20)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [7]=>
  int(1)
  [8]=>
  int(14)
  [9]=>
  int(1)
  [10]=>
  int(20)
}

-- Iteration 6 --
- With default sort flag -
bool(true)
array(2) {
  ["foo"]=>
  int(1)
  ["rab"]=>
  string(3) "zab"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(2) {
  ["foo"]=>
  int(1)
  ["rab"]=>
  string(3) "zab"
}

-- Iteration 7 --
- With default sort flag -
bool(true)
array(4) {
  ["a"]=>
  int(1)
  ["b"]=>
  array(2) {
    ["e"]=>
    int(3)
    ["f"]=>
    int(8)
  }
  ["c"]=>
  array(1) {
    ["g"]=>
    int(6)
  }
  ["d"]=>
  int(8)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(4) {
  ["a"]=>
  int(1)
  ["b"]=>
  array(2) {
    ["e"]=>
    int(3)
    ["f"]=>
    int(8)
  }
  ["c"]=>
  array(1) {
    ["g"]=>
    int(6)
  }
  ["d"]=>
  int(8)
}
Done
