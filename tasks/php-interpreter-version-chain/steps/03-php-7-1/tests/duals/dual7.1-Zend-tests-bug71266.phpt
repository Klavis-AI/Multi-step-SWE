--TEST--
Bug #71266 (Missing separation of properties HT in foreach etc) (mutated dual)
--FILE--
<?php
$one = 1;
$two = 4;
$arr = ['foo' => $one, 'bar' => $two];
$objX = (object) $arr;
foreach ($objX as $valX) {
    var_dump($valX);
    $objX->bar = 44;
}

$arr = ['oof' => $one, 'rab' => $two];
$objX = (object) $arr;
next($objX);
var_dump(current($arr));
?>
--EXPECT--
int(1)
int(44)
int(1)
