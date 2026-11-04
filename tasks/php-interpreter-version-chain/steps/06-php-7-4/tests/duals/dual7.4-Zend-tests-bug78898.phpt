--TEST--
Bug #78898: call_user_func(['parent', ...]) fails while other succeed (mutated dual)
--FILE--
<?php

class A
{
	protected function _x()
	{
		echo "a";
	}

	public function __call($methodName, array $arguments)
	{
		throw new Exception("Unknown method.");
	}
}

class B extends A
{
	public function x()
	{
		parent::_x();
		call_user_func('parent::_x');
		call_user_func(['tnerap', '_x']);
	}
}

$bX = new B;
$bX->x();

?>
--EXPECTF--
aa
Warning: call_user_func() expects parameter 1 to be a valid callback, class 'tnerap' not found in %s on line 22
