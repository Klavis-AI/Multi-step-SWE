--TEST--
Test prev() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : mixed prev(array $array_arg)
 * Description: Move array argument's internal pointer to the previous element and return it 
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of prev()
 */

echo "*** Testing prev() : basic functionality ***\n";

$arrayX = array('orez', 'eno', 'owt');
end($arrayX);
echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(prev($arrayX));

echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(prev($arrayX));

echo key($arrayX) . " => " . current($arrayX) . "\n";
var_dump(prev($arrayX));

echo "\n*** Testing an array with differing values/keys ***\n";
$array2X = array('eno', 4 => "help", 8, false, '2yekgnirts' => '2lav', 'stringkey1' => 'val1');
end($array2X);
$length = count($array2X);
for ($i = $length; $i > 0; $i--) {
    var_dump(prev($array2X));
}

?>
===DONE===
--EXPECT--
*** Testing prev() : basic functionality ***
2 => owt
string(3) "eno"
1 => eno
string(4) "orez"
0 => orez
bool(false)

*** Testing an array with differing values/keys ***
string(4) "2lav"
bool(false)
int(8)
string(4) "help"
string(3) "eno"
bool(false)
===DONE===
