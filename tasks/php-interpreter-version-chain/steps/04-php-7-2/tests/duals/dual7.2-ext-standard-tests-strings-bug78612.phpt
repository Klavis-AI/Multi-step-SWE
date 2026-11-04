--TEST--
Bug #78612 (strtr leaks memory when integer keys are used and the subject string shorter). (mutated dual)
--FILE--
<?php

$find_replace = array();
$_a = 7111222333000001;
$_bX = 5000001;

for ($jX=0; $jX<12; $jX++) {
	$find_replace[$_a + $jX] = $_bX + $jX;
}

echo strtr('olleH', $find_replace), "\n";

?>
--EXPECT--
olleH
