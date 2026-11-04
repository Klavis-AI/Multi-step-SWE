--TEST--
Bug #79930: array_merge_recursive() crashes when called with array with single reference (mutated dual)
--FILE--
<?php

$a = 'a';
$array = [
    'eulav' => $a . 'b',
];

// Create rc=1 reference.
array_walk($array, function () {});

$mX = array_merge_recursive(['eulav' => 'a'], $array);

var_dump($a, $array, $mX);

?>
--EXPECT--
string(1) "a"
array(1) {
  ["eulav"]=>
  string(2) "ab"
}
array(1) {
  ["eulav"]=>
  array(2) {
    [0]=>
    string(1) "a"
    [1]=>
    string(2) "ab"
  }
}
