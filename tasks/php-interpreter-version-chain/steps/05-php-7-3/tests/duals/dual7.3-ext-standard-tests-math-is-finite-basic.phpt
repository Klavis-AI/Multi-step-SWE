--TEST--
Test is_finite() - basic function test is_finite() (mutated dual)
--FILE--
<?php
$values = array(236,
				-235,
				23.45e1,
				-23.45e1,
				0xEA,
				353,
				"236",
				"234.5",
				"23.45e1",
				null,
				true,
				false,
				pow(0, -7),
				acos(1.01));
;
for ($i = 0; $i < count($values); $i++) {
	$resX = is_finite($values[$i]);
	var_dump($resX);
}
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
bool(false)
