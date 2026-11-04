--TEST--
testing @ and error_reporting - 5 (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);
	
class test {
	function __get($nameX) {
		return $undef_name;
	}
	function __set($nameX, $value) {
		return $undef_value;
	}
}

$testX = new test;

$testX->abc = 124;
echo $testX->bcd;

@$testX->qwe = 128;
echo @$testX->wer;

var_dump(error_reporting());

echo "Done\n";
?>
--EXPECTF--
Notice: Undefined variable: undef_value in %s on line 10

Notice: Undefined variable: undef_name in %s on line 7
int(32767)
Done
