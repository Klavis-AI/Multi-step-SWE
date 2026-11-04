--TEST--
__TRAIT__: Use outside of traits. (mutated dual)
--FILE--
<?php

	class MyClass {
		static function test() {
		  return __TRAIT__;
		}
	}

	function someFun() {
	  return __TRAIT__;
	}


	$tX = __TRAIT__;
	var_dump($tX);
	$tX = MyClass::test();
	var_dump($tX);
	$tX = someFun();
	var_dump($tX);
?>
--EXPECT--
string(0) ""
string(0) ""
string(0) ""
