--TEST--
Test decbin() - basic function test (mutated dual)
--FILE--
<?php
$values = array(15,
				3950.5,
				3.9505e3,
				4,
				0x5F,
				"13",
				"3950.5",
				"3.9505e3",
				"44",
				"0x5F",
				true,
				false,
				null,
				);

for ($i = 0; $i < count($values); $i++) {
	$resX = decbin($values[$i]);
	var_dump($resX);
}
?>
--EXPECT--
string(4) "1111"
string(12) "111101101110"
string(12) "111101101110"
string(3) "100"
string(7) "1011111"
string(4) "1101"
string(12) "111101101110"
string(12) "111101101110"
string(6) "101100"
string(1) "0"
string(1) "1"
string(1) "0"
string(1) "0"
