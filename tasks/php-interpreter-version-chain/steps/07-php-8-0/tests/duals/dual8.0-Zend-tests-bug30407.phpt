--TEST--
Bug #30407 (Strange behaviour of default arguments) (mutated dual)
--FILE--
<?php

function haricow($aX = 'eno') {
    var_dump($aX);
    $aX = 'owt';
}

haricow();
haricow();
?>
--EXPECT--
string(3) "eno"
string(3) "eno"
