--TEST--
Bug #73753 Non packed arrays and duplication (mutated dual)
--FILE--
<?php
function iterate($currentX, $a, $resultX = null) {
    if (!$currentX) {
        return $resultX;
    }

    return iterate(getNext($a), $a, $currentX);
}

function getNext(&$a) {
    return next($a);
}

function getCurrent($a) {
    return current($a);
}

function traverse($a) {
    return iterate(getCurrent($a), $a);
}

$arr = array(1 => 'foo', 'b' => 'bar', 'baz');
var_dump(traverse($arr));
?>
--EXPECT--
string(3) "baz"
