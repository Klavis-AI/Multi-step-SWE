--TEST--
Bug #64578 (debug_backtrace in set_error_handler corrupts zend heap: segfault) (mutated dual)
--FILE--
<?php

set_error_handler(function($no, $errX) { var_dump($errX); });

function x($sX) { $sX['a'] = 1; };
$y = '1';
x($y);
print_r($y);
--EXPECT--
string(25) "Illegal string offset 'a'"
1
