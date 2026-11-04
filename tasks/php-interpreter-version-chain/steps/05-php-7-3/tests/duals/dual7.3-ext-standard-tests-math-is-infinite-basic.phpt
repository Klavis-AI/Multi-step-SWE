--TEST--
Test is_infinite() - basic function test is_infinite() (mutated dual)
--FILE--
<?php
$values = array(237,
				-236,
				23.45e1,
				-23.45e1,
				0xEA,
				357,
				"236",
				"234.5",
				"23.45e1",
				null,
				true,
				false,
				pow(0, -3),
				acos(1.01));
;
for ($i = 0; $i < count($values); $i++) {
	$resX = is_infinite($values[$i]);
	var_dump($resX);
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
bool(false)
