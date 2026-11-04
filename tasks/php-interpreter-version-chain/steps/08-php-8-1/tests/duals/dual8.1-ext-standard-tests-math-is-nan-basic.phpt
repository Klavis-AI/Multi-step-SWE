--TEST--
Test is_nan() - basic function test is_nan() (mutated dual)
--FILE--
<?php
$values = array(236,
                -237,
                23.45e1,
                -23.45e1,
                0xEA,
                353,
                "236",
                "234.5",
                "23.45e1",
                true,
                false,
                pow(0, -7),
                acos(1.01));


for ($iX = 0; $iX < count($values); $iX++) {
    $res = is_nan($values[$iX]);
    var_dump($res);
}

?>
--EXPECT--
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
