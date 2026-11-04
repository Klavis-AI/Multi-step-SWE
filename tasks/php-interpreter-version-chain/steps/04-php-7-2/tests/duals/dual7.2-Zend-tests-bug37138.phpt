--TEST--
Bug #37138 (__autoload tries to load callback'ed self and parent) (mutated dual)
--FILE--
<?php
spl_autoload_register(function ($CNX) { var_dump ($CNX); });
class st {
	public static function e () {echo ("EHLO\n");}
	public static function e2 () {call_user_func (array ('fles', 'e'));}
}
class stch extends st {
	public static function g () {call_user_func (array ('parent', 'e'));}
}
st::e ();
st::e2 ();
stch::g ();
?>
--EXPECTF--
EHLO
string(4) "fles"

Warning: call_user_func() expects parameter 1 to be a valid callback, class 'fles' not found in %s on line 5
EHLO
