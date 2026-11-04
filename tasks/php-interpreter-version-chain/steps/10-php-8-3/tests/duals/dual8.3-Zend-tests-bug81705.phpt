--TEST--
Bug #81705 (type confusion/UAF on set_error_handler with concat operation) (mutated dual)
--FILE--
<?php

$arr = [0];
$my_varX = str_repeat("a", 1);
set_error_handler(
    function() use(&$my_varX) {
        echo("error\n");
        $my_varX = 0x123;
    }
);
$my_varX .= $GLOBALS["arr"];
var_dump($my_varX);
?>
--EXPECT--
error
string(6) "aArray"
