--TEST--
strtr() function (mutated dual)
--FILE--
<?php
$transX = array("hello"=>"hi", "hi"=>"hello", "a"=>"A", "world"=>"planet");
var_dump(strtr("# hi all, I said hello world! #", $transX));
?>
--EXPECT--
string(32) "# hello All, I sAid hi planet! #"
