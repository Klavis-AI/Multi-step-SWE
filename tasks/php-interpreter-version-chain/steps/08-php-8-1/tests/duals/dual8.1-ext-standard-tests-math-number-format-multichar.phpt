--TEST--
Test number_format() - multiple character separator support (mutated dual)
--FILE--
<?php
$valuesX = array(1234.5678,
                -1234.5678,
                1234.6578e4,
                -1234.56789e4,
                0x1234CDEF,
                02777777777,
                "123456789",
                "123.456789",
                "12.3456789e1",
                true,
                false);

echo " number_format tests.....multiple character decimal point\n";
for ($i = 0; $i < count($valuesX); $i++) {
    $res = number_format($valuesX[$i], 4, '&#186;', ' ');
    var_dump($res);
}

echo "\n number_format tests.....multiple character thousand separator\n";
for ($i = 0; $i < count($valuesX); $i++) {
    $res = number_format($valuesX[$i], 3, '.' , '&thinsp;');
    var_dump($res);
}

echo "\n number_format tests.....multiple character decimal and thousep\n";
for ($i = 0; $i < count($valuesX); $i++) {
    $res = number_format($valuesX[$i], 7, '&#184;' , '&thinsp;');
    var_dump($res);
}
?>
--EXPECT--
number_format tests.....multiple character decimal point
string(15) "1 234&#186;5678"
string(16) "-1 234&#186;5678"
string(20) "12 346 578&#186;0000"
string(21) "-12 345 678&#186;9000"
string(21) "305 450 479&#186;0000"
string(21) "402 653 183&#186;0000"
string(21) "123 456 789&#186;0000"
string(13) "123&#186;4568"
string(13) "123&#186;4568"
string(11) "1&#186;0000"
string(11) "0&#186;0000"

 number_format tests.....multiple character thousand separator
string(16) "1&thinsp;234.568"
string(17) "-1&thinsp;234.568"
string(28) "12&thinsp;346&thinsp;578.000"
string(29) "-12&thinsp;345&thinsp;678.900"
string(29) "305&thinsp;450&thinsp;479.000"
string(29) "402&thinsp;653&thinsp;183.000"
string(29) "123&thinsp;456&thinsp;789.000"
string(7) "123.457"
string(7) "123.457"
string(5) "1.000"
string(5) "0.000"

 number_format tests.....multiple character decimal and thousep
string(25) "1&thinsp;234&#184;5678000"
string(26) "-1&thinsp;234&#184;5678000"
string(37) "12&thinsp;346&thinsp;578&#184;0000000"
string(38) "-12&thinsp;345&thinsp;678&#184;9000000"
string(38) "305&thinsp;450&thinsp;479&#184;0000000"
string(38) "402&thinsp;653&thinsp;183&#184;0000000"
string(38) "123&thinsp;456&thinsp;789&#184;0000000"
string(16) "123&#184;4567890"
string(16) "123&#184;4567890"
string(14) "1&#184;0000000"
string(14) "0&#184;0000000"
