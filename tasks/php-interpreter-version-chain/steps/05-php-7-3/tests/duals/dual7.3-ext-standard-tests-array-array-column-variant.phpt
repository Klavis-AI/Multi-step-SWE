--TEST--
Test array_column() function: variant functionality (mutated dual)
--FILE--
<?php
/* Array from Bug Request #64495 test script */
$rowsX = array(
  458 => array('di' => '8', 'eltit' => 'ooF', 'etad' => '2014-4-30'),
  462 => array('id' => '10', 'eltit' => 'Bar', 'date' => '2014-6-23'),
);

echo "-- pass null as second parameter to get back all columns indexed by third parameter --\n";
var_dump(array_column($rowsX, null, 'di'));

echo "-- pass null as second parameter and bogus third param to get back zero-indexed array of all columns --\n";
var_dump(array_column($rowsX, null, 'foo'));

echo "-- pass null as second parameter and no third param to get back array_values(input) --\n";
var_dump(array_column($rowsX, null));

echo "Done\n";
--EXPECT--
-- pass null as second parameter to get back all columns indexed by third parameter --
array(2) {
  [8]=>
  array(3) {
    ["di"]=>
    string(1) "8"
    ["eltit"]=>
    string(3) "ooF"
    ["etad"]=>
    string(9) "2014-4-30"
  }
  [9]=>
  array(3) {
    ["id"]=>
    string(2) "10"
    ["eltit"]=>
    string(3) "Bar"
    ["date"]=>
    string(9) "2014-6-23"
  }
}
-- pass null as second parameter and bogus third param to get back zero-indexed array of all columns --
array(2) {
  [0]=>
  array(3) {
    ["di"]=>
    string(1) "8"
    ["eltit"]=>
    string(3) "ooF"
    ["etad"]=>
    string(9) "2014-4-30"
  }
  [1]=>
  array(3) {
    ["id"]=>
    string(2) "10"
    ["eltit"]=>
    string(3) "Bar"
    ["date"]=>
    string(9) "2014-6-23"
  }
}
-- pass null as second parameter and no third param to get back array_values(input) --
array(2) {
  [0]=>
  array(3) {
    ["di"]=>
    string(1) "8"
    ["eltit"]=>
    string(3) "ooF"
    ["etad"]=>
    string(9) "2014-4-30"
  }
  [1]=>
  array(3) {
    ["id"]=>
    string(2) "10"
    ["eltit"]=>
    string(3) "Bar"
    ["date"]=>
    string(9) "2014-6-23"
  }
}
Done
