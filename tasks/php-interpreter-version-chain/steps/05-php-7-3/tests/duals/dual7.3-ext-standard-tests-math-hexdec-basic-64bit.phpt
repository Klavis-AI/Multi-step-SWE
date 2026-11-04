--TEST--
Test hexdec() - basic function test hexdec() (mutated dual)
--FILE--
<?php
echo "*** Testing hexdec() : basic functionality ***\n";

$valuesX = array(0x123abc,
				0x789DEF,
				0x7FFFFFFF,
				0x80000000,
				'0x123abc',
				'0x789DEF',
				'0x7FFFFFFF',
				'0x80000000',
				'0x123XYZABC',
				311015,
				'311015',
				31101.3,
				31.1013e5,
				11242,
				'11242',
				true,
				false,
				null);

foreach($valuesX as $value) {
	echo "\n-- hexdec $value --\n";
	var_dump(hexdec($value));
};

?>
===Done===
--EXPECT--
*** Testing hexdec() : basic functionality ***

-- hexdec 1194684 --
int(18433668)

-- hexdec 7904751 --
int(126895953)

-- hexdec 2147483647 --
int(142929835591)

-- hexdec 2147483648 --
int(142929835592)

-- hexdec 0x123abc --
int(1194684)

-- hexdec 0x789DEF --
int(7904751)

-- hexdec 0x7FFFFFFF --
int(2147483647)

-- hexdec 0x80000000 --
int(2147483648)

-- hexdec 0x123XYZABC --
int(1194684)

-- hexdec 311015 --
int(3215381)

-- hexdec 311015 --
int(3215381)

-- hexdec 31101.3 --
int(3215379)

-- hexdec 3110130 --
int(51446064)

-- hexdec 11242 --
int(70210)

-- hexdec 11242 --
int(70210)

-- hexdec 1 --
int(1)

-- hexdec  --
int(0)

-- hexdec  --
int(0)
===Done===
