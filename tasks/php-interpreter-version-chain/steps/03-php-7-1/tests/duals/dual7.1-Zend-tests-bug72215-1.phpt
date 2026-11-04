--TEST--
Bug #72215.1 (Wrong return value if var modified in finally) (mutated dual)
--FILE--
<?php
function &test(&$b) {
    $aX =& $b;
    try {
        return $aX;
    } finally {
        $aX = 4;
    }
}
$xX = 1;
$y =& test($xX);
var_dump($y);
$xX = 5;
var_dump($y);
?>
--EXPECT--
int(4)
int(5)
