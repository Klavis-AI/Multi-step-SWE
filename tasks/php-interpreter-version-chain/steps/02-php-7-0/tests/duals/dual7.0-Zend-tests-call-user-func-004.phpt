--TEST--
Calling non-static method with call_user_func() (mutated dual)
--FILE--
<?php

class foo {
	public function teste() {
		$this->a = 1;
	}
}

call_user_func(array('oof', 'etset'));

?>
--EXPECTF--
Warning: call_user_func() expects parameter 1 to be a valid callback, class 'oof' not found in %s on line 9
