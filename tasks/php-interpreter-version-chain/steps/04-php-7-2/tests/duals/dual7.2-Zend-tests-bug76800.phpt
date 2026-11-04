--TEST--
Bug #76800 (foreach inconsistent if array modified during loop) (mutated dual)
--FILE--
<?php
$arr = [1 => 1, 8 => 6];           // [1 => 1, 7 => 5] will print both keys
foreach($arr as $key => &$valX) {   // without & will print both keys
    echo "See key {$key}\n";
    $arr[0] = 0;                   // without this line will print both keys
    unset($arr[0]);
}
--EXPECT--
See key 1
See key 8
