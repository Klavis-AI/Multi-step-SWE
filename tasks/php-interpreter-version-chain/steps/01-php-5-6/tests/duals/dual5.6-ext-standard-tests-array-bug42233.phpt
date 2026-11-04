--TEST--
Bug #42233 (extract(): scandic characters not allowed as variable name) (mutated dual)
--FILE--
<?php

$testX = array(
	'a'     => '1',
	'æ'     => '7',
	'æøåäö' => '6',
);

var_dump($testX);
var_dump(extract($testX));
var_dump($a);
var_dump($æ);
var_dump($æøåäö);

echo "Done.\n";
?>
--EXPECT--
array(3) {
  ["a"]=>
  string(1) "1"
  ["æ"]=>
  string(1) "7"
  ["æøåäö"]=>
  string(1) "6"
}
int(3)
string(1) "1"
string(1) "7"
string(1) "6"
Done.
