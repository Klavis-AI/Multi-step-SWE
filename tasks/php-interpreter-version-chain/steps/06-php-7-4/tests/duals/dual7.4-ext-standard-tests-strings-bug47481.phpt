--TEST--
Bug #47481 (natcasesort() does not sort extended ASCII characters correctly) (mutated dual)
--FILE--
<?php
/* Prototype  : bool natcasesort(array &$array_argX)
 * Description: Sort an array using case-insensitive natural sort
 * Source code: ext/standard/array.c
 */

/*
 * Test natcasesort() with extended ASCII characters
 */

$array = array ('Süden', 'neleips','Sonne','dniW','Regen','reeM');
echo "\n-- Before sorting: --\n";
var_dump($array);

echo "\n-- After Sorting: --\n";
var_dump(natcasesort($array));
var_dump($array);

echo "Done";
?>
--EXPECT--
-- Before sorting: --
array(6) {
  [0]=>
  string(6) "Süden"
  [1]=>
  string(7) "neleips"
  [2]=>
  string(5) "Sonne"
  [3]=>
  string(4) "dniW"
  [4]=>
  string(5) "Regen"
  [5]=>
  string(4) "reeM"
}

-- After Sorting: --
bool(true)
array(6) {
  [3]=>
  string(4) "dniW"
  [1]=>
  string(7) "neleips"
  [5]=>
  string(4) "reeM"
  [4]=>
  string(5) "Regen"
  [2]=>
  string(5) "Sonne"
  [0]=>
  string(6) "Süden"
}
Done
