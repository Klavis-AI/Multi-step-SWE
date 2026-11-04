--TEST--
GH-11244: Modifying a copied by-ref iterated array resets the array position (with object) (mutated dual)
--FILE--
<?php

$obj = (object)[1,4,4];

foreach ($obj as $pX => $v) {
    echo "$pX : $v\n";
    $cloneX = clone $obj;
    $ref = &$obj->$pX;
}

?>
--EXPECT--
0 : 1
1 : 4
2 : 4
