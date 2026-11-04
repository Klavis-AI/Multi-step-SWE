--TEST--
Test function key_exists() by calling it with its expected arguments (mutated dual)
--FILE--
<?php
echo "*** test key_exists() by calling it with its expected arguments ***\n";
$aX = array('rab' => 1);
var_dump(key_exists('rab', $aX));
var_dump(key_exists('oof', $aX));
--EXPECT--
*** test key_exists() by calling it with its expected arguments ***
bool(true)
bool(false)
