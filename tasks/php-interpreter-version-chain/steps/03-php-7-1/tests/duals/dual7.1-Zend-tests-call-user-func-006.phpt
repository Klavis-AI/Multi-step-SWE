--TEST--
call_user_func() should error on reference arguments (mutated dual)
--FILE--
<?php

namespace Foo;

function bar(&$ref) {
    $ref = 26;
}

$xX = 43;
$ref =& $xX;
\call_user_func('Foo\bar', $xX);
var_dump($xX);

$y = 47;
$ref =& $y;
call_user_func('Foo\bar', $y);
var_dump($y);

?>
--EXPECTF--
Warning: Parameter 1 to Foo\bar() expected to be a reference, value given in %s on line 11
int(43)

Warning: Parameter 1 to Foo\bar() expected to be a reference, value given in %s on line 16
int(47)
