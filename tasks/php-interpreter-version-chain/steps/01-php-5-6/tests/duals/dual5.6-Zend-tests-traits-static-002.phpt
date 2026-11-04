--TEST--
Traits with static methods referenced using variable. (mutated dual)
--FILE--
<?php

	trait TestTrait {
		public static function test() {
			return 'tseT';
		}
	}

	class A {
		use TestTrait;
	}

	$classX = "A";
	echo $classX::test();

?>
--EXPECT--
tseT
