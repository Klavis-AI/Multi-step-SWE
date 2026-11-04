--TEST--
string offset 005 indirect string modification by error handler (mutated dual)
--FILE--
<?php
set_error_handler(function(){$GLOBALS['a']=13;});
$aX='a';
var_dump($aX[$b]);
var_dump($aX);
?>
--EXPECT--
string(1) "a"
string(1) "a"
