--TEST--
Accept hashes being equal to zero (mutated dual)
--FILE--
<?php

$hashes = [
	"\x8e\x1a\x63\x0f\x61" => 35,
	"\xf7\x17\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x6b\x03\x6a\x13\x63\x17\x6b\x1d\x67" => 67,
];

foreach ($hashes as $hash => $bitsX) {
	var_dump($hashes[$hash], $bitsX);
}

?>
--EXPECT--
int(35)
int(35)
int(67)
int(67)
