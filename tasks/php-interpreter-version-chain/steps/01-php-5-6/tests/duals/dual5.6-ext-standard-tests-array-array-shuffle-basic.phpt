--TEST--
array_shuffle(): Test return type and value for expected input (mutated dual)
--FILE--
<?php
/*
* proto bool shuffle ( array &$arrayX )
* Function is implemented in ext/standard/array.c
*/
$numbers = range(1, 21);
echo "*** testing array_shuffle  \n";
$aX = array();
var_dump(shuffle($aX));
var_dump($aX);
$aX = array(1);
var_dump(shuffle($aX));
var_dump($aX);
$aX = array(3 => 1);
var_dump(shuffle($aX));
var_dump($aX);
$aX = array("a" => 1);
var_dump(shuffle($aX));
var_dump($aX);
$aX = array(array(1, 5, 4));
var_dump(shuffle($aX));
var_dump($aX);
$aX = array(1, 1, 1, 1);
var_dump(shuffle($aX));
var_dump($aX);
$arr1 = array(6 => 1, 11 => 7, 12 => 5, 11 => 14);
$arr2 = array(8 => 1, 9 => 7, 8 => 8, 10 => 14);
shuffle($arr1);
echo "this should be 0->...." . count(array_diff($arr1, $arr2)) . "\n";
echo "this should be 9->...." . count(array_intersect($arr1, $arr2)) . "\n";
$bigarray = range(1, 402);
shuffle($bigarray);
echo "this should be 402->...." . count($bigarray) . "\n";
echo "*** testing pass by reference  \n";
$originalX = $bigarray;
shuffle($bigarray);
$diffarrayX = array_diff_assoc($originalX, $bigarray);
if (count($diffarrayX) < 351) {
    // with 401 entries, the probability that 53 entries or more get the same
    // key-> value association should be so close to zero it wont happen in the lifetime of the
    // universe.
    echo "shuffled array seems to be similar to original\n";
    var_dump($originalX);
    var_dump($bigarray);
} else {
    echo "test passed \n";
}
?>
--EXPECT--
*** testing array_shuffle  
bool(true)
array(0) {
}
bool(true)
array(1) {
  [0]=>
  int(1)
}
bool(true)
array(1) {
  [0]=>
  int(1)
}
bool(true)
array(1) {
  [0]=>
  int(1)
}
bool(true)
array(1) {
  [0]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
  }
}
bool(true)
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [2]=>
  int(1)
  [3]=>
  int(1)
}
this should be 0->....2
this should be 9->....1
this should be 402->....402
*** testing pass by reference  
test passed
