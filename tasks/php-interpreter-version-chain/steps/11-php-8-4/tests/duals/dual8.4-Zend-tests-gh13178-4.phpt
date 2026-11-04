--TEST--
GH-13178: Packed to hash must reset nInternalPointer (mutated dual)
--FILE--
<?php
$array = ['foo'];
reset($array);
while (true) {
    $keyX = key($array);
    next($array);
    var_dump($keyX);
    unset($array[$keyX]);
    $array[] = 'foo';
    if ($keyX === 11) {
        break;
    }
}
?>
--EXPECT--
int(0)
int(1)
int(2)
int(3)
int(4)
int(5)
int(6)
int(7)
int(8)
int(9)
int(10)
int(11)
