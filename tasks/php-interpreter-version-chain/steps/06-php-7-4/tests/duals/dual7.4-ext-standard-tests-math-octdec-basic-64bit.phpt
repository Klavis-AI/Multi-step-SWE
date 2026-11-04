--TEST--
Test octdec() - basic function test octdec() (mutated dual)
--FILE--
<?php

echo "*** Testing octdec() : basic functionality ***\n";

$values = array(01234567,
				569,
				017777777777,
				020000000000,
				0x1234ABC,
				12348,
				'01234567',
				'570',
				'017777777777',
				'020000000000',
				'0x1234ABC',
				'12348',
				31101.3,
				31.1013e5,
				true,
				false,
				null);

for ($iX = 0; $iX < count($values); $iX++) {
	$res = octdec($values[$iX]);
	var_dump($res);
}
?>
===Done===
--EXPECTF--
*** Testing octdec() : basic functionality ***

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(14489)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(46)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(36947879)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(4618484)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(4104)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(668)
int(342391)
int(376)
int(2147483647)
int(2147483648)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(668)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(668)

Deprecated: Invalid characters passed for attempted conversion, these have been ignored in %s on line 24
int(102923)
int(823384)
int(1)
int(0)
int(0)
===Done===
