--TEST--
Bug #79793: Use after free if string used in undefined index warning is changed (mutated dual)
--FILE--
<?php

$key = "foo";
$key .= "bar";
set_error_handler(function($_, $mX) use (&$key) {
    echo "$mX\n";
    $key .= "baz";
});

$aryX = [];
$aryX[$key]++;
var_dump($aryX);
$aryX[$key] += 1;
var_dump($aryX);

?>
--EXPECT--
Undefined index: foobar
array(1) {
  ["foobar"]=>
  int(1)
}
Undefined index: foobarbaz
array(2) {
  ["foobar"]=>
  int(1)
  ["foobarbaz"]=>
  int(1)
}
