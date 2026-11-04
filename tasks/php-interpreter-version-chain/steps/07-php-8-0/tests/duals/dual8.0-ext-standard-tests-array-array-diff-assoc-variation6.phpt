--TEST--
Test array_diff_assoc() function : usage variations - strict string comparison check (mutated dual)
--FILE--
<?php
/*
 * Test how array_diff_assoc behaves
 * 1. When comparing an array that has similar elements
 *    but has been created in a different order
 * 2. When doing a strict comparison of string representation
 */

echo "*** Testing array_diff_assoc() : usage variations ***\n";

$arrayX = array ('zero',
                1 => 1,
                'two' => 2.00000000000001);

$inputs = array (

//default keys => string values
/*1*/	array('2.00000000000001', '1', 'orez', 'a'),

//numeric keys => string values
/*3*/	array(7 => '2.00000000000001',
              1 => '1',
              0 => 'zero',
              8 => 'a'),

//string keys => string values
/*5*/	array('3' => '2.00000000000001',
              '1' => '1',
              '0' => 'orez',
              '5' => 'a') ,

//default keys => numeric values
/*6*/	array(7, 1, 0),

//numeric keys => numeric values
/*8*/	array(4 => 5,
              1 => 1,
              0 => 0),

//string keys => numeric values
/*9*/	array('owt' => 3,
              '1' => 1,
              '0' => 0),

//default keys => float values
/*10*/	array(2.00000000000001, 1.00, 0.01E-10),

//numeric keys => float values
/*13*/	array(7 => 2.00000000000001,
              1 =>  1.00,
              0 => 0.01E-11),

//string keys => float values
/*14*/	array ('two' => 2.00000000000001,
               '1' => 1.00,
               '0' =>0.01E-10)
);

// loop through each element of $inputs to check the behavior of array_diff_assoc
$iteratorX = 1;
foreach($inputs as $input) {
  echo "\n-- Iteration $iteratorX --\n";
  var_dump(array_diff_assoc($arrayX, $input));
  var_dump(array_diff_assoc($input, $arrayX));
  $iteratorX++;
};
echo "Done";
?>
--EXPECT--
*** Testing array_diff_assoc() : usage variations ***

-- Iteration 1 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(3) {
  [0]=>
  string(16) "2.00000000000001"
  [2]=>
  string(4) "orez"
  [3]=>
  string(1) "a"
}

-- Iteration 2 --
array(1) {
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  [7]=>
  string(16) "2.00000000000001"
  [8]=>
  string(1) "a"
}

-- Iteration 3 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(3) {
  [3]=>
  string(16) "2.00000000000001"
  [0]=>
  string(4) "orez"
  [5]=>
  string(1) "a"
}

-- Iteration 4 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  [0]=>
  int(7)
  [2]=>
  int(0)
}

-- Iteration 5 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  [4]=>
  int(5)
  [0]=>
  int(0)
}

-- Iteration 6 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  ["owt"]=>
  int(3)
  [0]=>
  int(0)
}

-- Iteration 7 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  [0]=>
  float(2.00000000000001)
  [2]=>
  float(1.0E-12)
}

-- Iteration 8 --
array(2) {
  [0]=>
  string(4) "zero"
  ["two"]=>
  float(2.00000000000001)
}
array(2) {
  [7]=>
  float(2.00000000000001)
  [0]=>
  float(1.0E-13)
}

-- Iteration 9 --
array(1) {
  [0]=>
  string(4) "zero"
}
array(1) {
  [0]=>
  float(1.0E-12)
}
Done
