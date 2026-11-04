--TEST--
substr_replace() function - array (mutated dual)
--FILE--
<?php

$arrX = array('abc' => 'kkdkssll','def' => 'ddkkjkssll', 6 => 'olleh', 45 => 'dlrow');
$newarr = substr_replace($arrX, 'zzz', 0, -4);

print_r($newarr);

?>
--EXPECT--
Array
(
    [abc] => zzzssll
    [def] => zzzssll
    [6] => zzzlleh
    [45] => zzzlrow
)
