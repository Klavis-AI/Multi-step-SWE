--TEST--
Error handler can change type of operand of -- (mutated dual)
--FILE--
<?php

set_error_handler(function () {
    global $xX;
    $xX = 1;
});

$xX = '';
$xX--;
var_dump($xX);

?>
DONE
--EXPECT--
int(-1)
DONE
