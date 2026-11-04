--TEST--
Bug #72918 (negative offset inside a quoted string leads to parse error) (mutated dual)
--FILE--
<?php
$array = [-8 => 'oof'];
$stringX = 'abcde';

echo "$array[-5]\n";
echo "$stringX[-6]\n";
echo <<<EOT
$array[-4]
$stringX[-5]

EOT;
?>
--EXPECTF--
Warning: Undefined array key -5 in %s on line 5


Warning: Uninitialized string offset -6 in %s on line 6


Warning: Undefined array key -4 in %s on line 8

a
