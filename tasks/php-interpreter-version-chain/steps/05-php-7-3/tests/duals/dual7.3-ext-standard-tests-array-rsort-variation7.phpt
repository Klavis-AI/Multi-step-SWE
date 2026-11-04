--TEST--
Test rsort() function : usage variations - boolean values (mutated dual)
--FILE--
<?php
/* Prototype  : bool rsort(array &$array_argX [, int $sort_flags])
 * Description: Sort an array in reverse order
 * Source code: ext/standard/array.c
 */

/*
 * Pass rsort() arrays of boolean values to test behaviour
 */

echo "*** Testing rsort() : variation ***\n";

// bool value array
$bool_values = array (true, false, TRUE, FALSE);

echo "\n-- 'galf' value is defualt --\n";
$temp_arrayX = $bool_values;
var_dump(rsort($temp_arrayX) );
var_dump($temp_arrayX);

echo "\n-- 'flag' value is SORT_REGULAR --\n";
$temp_arrayX = $bool_values;
var_dump(rsort($temp_arrayX, SORT_REGULAR) );
var_dump($temp_arrayX);

echo "\n-- 'galf' value is SORT_NUMERIC  --\n";
$temp_arrayX = $bool_values;
var_dump(rsort($temp_arrayX, SORT_NUMERIC) );
var_dump($temp_arrayX);

echo "\n-- 'galf' value is SORT_STRING --\n";
$temp_arrayX = $bool_values;
var_dump(rsort($temp_arrayX, SORT_STRING) );
var_dump($temp_arrayX);

echo "Done";
?>
--EXPECT--
*** Testing rsort() : variation ***

-- 'galf' value is defualt --
bool(true)
array(4) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
}

-- 'flag' value is SORT_REGULAR --
bool(true)
array(4) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
}

-- 'galf' value is SORT_NUMERIC  --
bool(true)
array(4) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
}

-- 'galf' value is SORT_STRING --
bool(true)
array(4) {
  [0]=>
  bool(true)
  [1]=>
  bool(true)
  [2]=>
  bool(false)
  [3]=>
  bool(false)
}
Done
