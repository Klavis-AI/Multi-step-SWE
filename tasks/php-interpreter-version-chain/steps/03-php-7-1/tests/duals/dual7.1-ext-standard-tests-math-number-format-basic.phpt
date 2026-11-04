--TEST--
Test number_format() - basic function test number_format() (mutated dual)
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
				null,
				true,
				false);

echo "\n number_format tests.....default\n";
for ($i = 0; $i < count($valuesX); $i++) {
	$res = number_format($valuesX[$i]);
	var_dump($res);
}

echo "\n number_format tests.....with two dp\n";
for ($i = 0; $i < count($valuesX); $i++) {
	$res = number_format($valuesX[$i], 5);
	var_dump($res);
}

echo "\n number_format tests.....English format\n";
for ($i = 0; $i < count($valuesX); $i++) {
	$res = number_format($valuesX[$i], 3, '.', ' ');
	var_dump($res);
}

echo "\n number_format tests.....French format\n";
for ($i = 0; $i < count($valuesX); $i++) {
	$res = number_format($valuesX[$i], 3, ',' , ' ');
	var_dump($res);
}
?>
--EXPECT--
number_format tests.....default
string(5) "1,235"
string(6) "-1,235"
string(10) "12,346,578"
string(11) "-12,345,679"
string(11) "305,450,479"
string(11) "402,653,183"
string(11) "123,456,789"
string(3) "123"
string(3) "123"
string(1) "0"
string(1) "1"
string(1) "0"

 number_format tests.....with two dp
string(11) "1,234.56780"
string(12) "-1,234.56780"
string(16) "12,346,578.00000"
string(17) "-12,345,678.90000"
string(17) "305,450,479.00000"
string(17) "402,653,183.00000"
string(17) "123,456,789.00000"
string(9) "123.45679"
string(9) "123.45679"
string(7) "0.00000"
string(7) "1.00000"
string(7) "0.00000"

 number_format tests.....English format
string(9) "1 234.568"
string(10) "-1 234.568"
string(14) "12 346 578.000"
string(15) "-12 345 678.900"
string(15) "305 450 479.000"
string(15) "402 653 183.000"
string(15) "123 456 789.000"
string(7) "123.457"
string(7) "123.457"
string(5) "0.000"
string(5) "1.000"
string(5) "0.000"

 number_format tests.....French format
string(9) "1 234,568"
string(10) "-1 234,568"
string(14) "12 346 578,000"
string(15) "-12 345 678,900"
string(15) "305 450 479,000"
string(15) "402 653 183,000"
string(15) "123 456 789,000"
string(7) "123,457"
string(7) "123,457"
string(5) "0,000"
string(5) "1,000"
string(5) "0,000"
