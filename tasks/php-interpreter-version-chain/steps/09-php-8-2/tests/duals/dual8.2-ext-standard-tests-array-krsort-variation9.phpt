--TEST--
Test krsort() function : usage variations - sort array with/without key values (mutated dual)
--FILE--
<?php
/*
 * Testing krsort() by providing arrays  with/without key values for $arrayX argument
 * with following flag values:
 *  1.flag value as default
 *  2.SORT_REGULAR - compare items normally
 */

echo "*** Testing krsort() : usage variations ***\n";

// list of arrays with/without key values
$various_arrays = array (
  array(10 => 56,  71, 25, 34, 13),
  array ("a" => "orange",  "banana", "c" => "apple"),
  array(1, 3, 5, 6, 7, 9),
  array("first", 7 => "second", 1 => "third"),
  array(1, 1, 10 => 1,  9 => 1, 22, 6 => 15),
  array('bar' => 'zab', "foo" => 1),
  array('a' => 1,'b' => array('e' => 5,'f' => 4),'c' => array('g' => 9),'d' => 8),
);

$countX = 1;
echo "\n-- Testing krsort() by supplying various arrays with/without key values --\n";

// loop through to test krsort() with different arrays,
foreach ($various_arrays as $arrayX) {
  echo "\n-- Iteration $countX --\n";

  echo "- With default sort flag -\n";
  $temp_array = $arrayX;
  var_dump( krsort($temp_array) );
  var_dump($temp_array);

  echo "- Sort flag = SORT_REGULAR -\n";
  $temp_array = $arrayX;
  var_dump( krsort($temp_array, SORT_REGULAR) );
  var_dump($temp_array);
  $countX++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing krsort() : usage variations ***

-- Testing krsort() by supplying various arrays with/without key values --

-- Iteration 1 --
- With default sort flag -
bool(true)
array(5) {
  [14]=>
  int(13)
  [13]=>
  int(34)
  [12]=>
  int(25)
  [11]=>
  int(71)
  [10]=>
  int(56)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(5) {
  [14]=>
  int(13)
  [13]=>
  int(34)
  [12]=>
  int(25)
  [11]=>
  int(71)
  [10]=>
  int(56)
}

-- Iteration 2 --
- With default sort flag -
bool(true)
array(3) {
  ["c"]=>
  string(5) "apple"
  ["a"]=>
  string(6) "orange"
  [0]=>
  string(6) "banana"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(3) {
  ["c"]=>
  string(5) "apple"
  ["a"]=>
  string(6) "orange"
  [0]=>
  string(6) "banana"
}

-- Iteration 3 --
- With default sort flag -
bool(true)
array(6) {
  [5]=>
  int(9)
  [4]=>
  int(7)
  [3]=>
  int(6)
  [2]=>
  int(5)
  [1]=>
  int(3)
  [0]=>
  int(1)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(6) {
  [5]=>
  int(9)
  [4]=>
  int(7)
  [3]=>
  int(6)
  [2]=>
  int(5)
  [1]=>
  int(3)
  [0]=>
  int(1)
}

-- Iteration 4 --
- With default sort flag -
bool(true)
array(3) {
  [7]=>
  string(6) "second"
  [1]=>
  string(5) "third"
  [0]=>
  string(5) "first"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(3) {
  [7]=>
  string(6) "second"
  [1]=>
  string(5) "third"
  [0]=>
  string(5) "first"
}

-- Iteration 5 --
- With default sort flag -
bool(true)
array(6) {
  [11]=>
  int(22)
  [10]=>
  int(1)
  [9]=>
  int(1)
  [6]=>
  int(15)
  [1]=>
  int(1)
  [0]=>
  int(1)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(6) {
  [11]=>
  int(22)
  [10]=>
  int(1)
  [9]=>
  int(1)
  [6]=>
  int(15)
  [1]=>
  int(1)
  [0]=>
  int(1)
}

-- Iteration 6 --
- With default sort flag -
bool(true)
array(2) {
  ["foo"]=>
  int(1)
  ["bar"]=>
  string(3) "zab"
}
- Sort flag = SORT_REGULAR -
bool(true)
array(2) {
  ["foo"]=>
  int(1)
  ["bar"]=>
  string(3) "zab"
}

-- Iteration 7 --
- With default sort flag -
bool(true)
array(4) {
  ["d"]=>
  int(8)
  ["c"]=>
  array(1) {
    ["g"]=>
    int(9)
  }
  ["b"]=>
  array(2) {
    ["e"]=>
    int(5)
    ["f"]=>
    int(4)
  }
  ["a"]=>
  int(1)
}
- Sort flag = SORT_REGULAR -
bool(true)
array(4) {
  ["d"]=>
  int(8)
  ["c"]=>
  array(1) {
    ["g"]=>
    int(9)
  }
  ["b"]=>
  array(2) {
    ["e"]=>
    int(5)
    ["f"]=>
    int(4)
  }
  ["a"]=>
  int(1)
}
Done
